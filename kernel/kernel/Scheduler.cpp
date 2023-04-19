#include <kernel/Arch.h>
#include <kernel/Attributes.h>
#include <kernel/CriticalScope.h>
#include <kernel/InterruptController.h>
#include <kernel/Process.h>
#include <kernel/Scheduler.h>

#if 1
	#define VERIFY_STI() ASSERT(interrupts_enabled())
	#define VERIFY_CLI() ASSERT(!interrupts_enabled())
#else
	#define VERIFY_STI()
	#define VERIFY_CLI()
#endif

namespace Kernel
{

	extern "C" [[noreturn]] void start_thread(uintptr_t rsp, uintptr_t rip);
	extern "C" [[noreturn]] void continue_thread(uintptr_t rsp, uintptr_t rip);
	extern "C" uintptr_t read_rip();

	static Scheduler* s_instance = nullptr;

	BAN::ErrorOr<void> Scheduler::initialize()
	{
		ASSERT(s_instance == nullptr);
		s_instance = new Scheduler();
		ASSERT(s_instance);
		s_instance->m_idle_thread = TRY(Thread::create([](void*) { for (;;) asm volatile("hlt"); }, nullptr, nullptr));
		return {};
	}

	Scheduler& Scheduler::get()
	{
		ASSERT(s_instance);
		return *s_instance;
	}

	void Scheduler::start()
	{
		VERIFY_CLI();
		ASSERT(!m_active_threads.empty());
		m_current_thread = m_active_threads.begin();
		execute_current_thread();
		ASSERT_NOT_REACHED();
	}

	Thread& Scheduler::current_thread()
	{
		return m_current_thread ? *m_current_thread->thread : *m_idle_thread;
	}

	void Scheduler::reschedule()
	{
		VERIFY_CLI();
		ASSERT(InterruptController::get().is_in_service(PIT_IRQ));
		InterruptController::get().eoi(PIT_IRQ);

		if (PIT::ms_since_boot() <= m_last_reschedule)
			return;
		m_last_reschedule = PIT::ms_since_boot();
		
		wake_threads();

		if (save_current_thread())
			return;
		advance_current_thread();
		execute_current_thread();
		ASSERT_NOT_REACHED();
	}

	void Scheduler::reschedule_if_idling()
	{
		VERIFY_CLI();

		if (m_active_threads.empty() || &current_thread() != m_idle_thread)
			return;
		
		if (save_current_thread())
			return;
		m_current_thread = m_active_threads.begin();
		execute_current_thread();
		ASSERT_NOT_REACHED();
	}

	void Scheduler::wake_threads()
	{
		VERIFY_CLI();

		uint64_t current_time = PIT::ms_since_boot();
		while (!m_sleeping_threads.empty() && m_sleeping_threads.front().wake_time <= current_time)
		{
			Thread* thread = m_sleeping_threads.front().thread;
			m_sleeping_threads.remove(m_sleeping_threads.begin());

			// This should work as we released enough memory from sleeping thread
			static_assert(sizeof(ActiveThread) == sizeof(SleepingThread));
			MUST(m_active_threads.emplace_back(thread));
		}
	}

	BAN::ErrorOr<void> Scheduler::add_thread(Thread* thread)
	{
		Kernel::CriticalScope critical;
		TRY(m_active_threads.emplace_back(thread));
		return {};
	}

	void Scheduler::advance_current_thread()
	{
		VERIFY_CLI();

		if (m_active_threads.empty())
		{
			m_current_thread = {};
			return;
		}
		if (!m_current_thread || ++m_current_thread == m_active_threads.end())
			m_current_thread = m_active_threads.begin();
	}

	void Scheduler::remove_and_advance_current_thread()
	{
		VERIFY_CLI();

		ASSERT(m_current_thread);

		if (m_active_threads.size() == 1)
		{
			m_active_threads.remove(m_current_thread);
			m_current_thread = {};
		}
		else
		{
			auto temp = m_current_thread;
			advance_current_thread();
			m_active_threads.remove(temp);
		}
	}

	// NOTE: this is declared always inline, so we don't corrupt the stack
	//       after getting the rsp
	ALWAYS_INLINE bool Scheduler::save_current_thread()
	{
		VERIFY_CLI();

		uintptr_t rsp, rip;
		push_callee_saved();
		if (!(rip = read_rip()))
		{
			pop_callee_saved();
			return true;
		}
		read_rsp(rsp);

		Thread& current = current_thread();
		current.set_rip(rip);
		current.set_rsp(rsp);

		ASSERT(current.stack_base() <= rsp && rsp <= current.stack_base() + current.stack_size());

		return false;
	}

	void Scheduler::execute_current_thread()
	{
		VERIFY_CLI();
		
		Thread& current = current_thread();

		switch (current.state())
		{
			case Thread::State::NotStarted:
				current.set_started();
				start_thread(current.rsp(), current.rip());
			case Thread::State::Executing:
				continue_thread(current.rsp(), current.rip());
			case Thread::State::Terminating:
				ENABLE_INTERRUPTS();
				current.on_exit();
				ASSERT_NOT_REACHED();
		}

		ASSERT_NOT_REACHED();
	}

	void Scheduler::set_current_thread_sleeping(uint64_t wake_time)
	{
		VERIFY_STI();
		DISABLE_INTERRUPTS();

		ASSERT(m_current_thread);

		Thread* sleeping = m_current_thread->thread;

		if (save_current_thread())
		{
			ENABLE_INTERRUPTS();
			return;
		}
		remove_and_advance_current_thread();

		auto it = m_sleeping_threads.begin();
		for (; it != m_sleeping_threads.end(); it++)
			if (wake_time <= it->wake_time)
				break;

		// This should work as we released enough memory from active thread
		static_assert(sizeof(ActiveThread) == sizeof(SleepingThread));
		MUST(m_sleeping_threads.emplace(it, sleeping, wake_time));

		execute_current_thread();
		ASSERT_NOT_REACHED();
	}

	void Scheduler::set_current_thread_done()
	{
		VERIFY_STI();
		DISABLE_INTERRUPTS();

		ASSERT(m_current_thread);

		Thread* thread = m_current_thread->thread;
		remove_and_advance_current_thread();
		delete thread;

		execute_current_thread();
		ASSERT_NOT_REACHED();
	}

#define remove_threads(list, condition)				\
	for (auto it = list.begin(); it != list.end();) \
	{												\
		if (condition)								\
		{											\
			delete it->thread;						\
			it = list.remove(it);					\
		}											\
		else										\
		{											\
			it++;									\
		}											\
	}

	void Scheduler::set_current_process_done()
	{
		VERIFY_STI();
		DISABLE_INTERRUPTS();

		pid_t pid = m_current_thread->thread->process().pid();

		remove_threads(m_blocking_threads, it->thread->process().pid() == pid);
		remove_threads(m_sleeping_threads, it->thread->process().pid() == pid);
		remove_threads(m_active_threads, it != m_current_thread && it->thread->process().pid() == pid);

		delete &m_current_thread->thread->process();
		delete m_current_thread->thread;
		remove_and_advance_current_thread();
		execute_current_thread();

		ASSERT_NOT_REACHED();
	}

	void Scheduler::block_current_thread(Semaphore* semaphore)
	{
		VERIFY_STI();
		DISABLE_INTERRUPTS();

		ASSERT(m_current_thread);

		Thread* blocking = m_current_thread->thread;

		if (save_current_thread())
		{
			ENABLE_INTERRUPTS();
			return;
		}
		remove_and_advance_current_thread();

		// This should work as we released enough memory from active thread
		static_assert(sizeof(ActiveThread) == sizeof(BlockingThread));
		MUST(m_blocking_threads.emplace_back(blocking, semaphore));

		semaphore->m_blocked = true;

		execute_current_thread();
		ASSERT_NOT_REACHED();
	}
	
	void Scheduler::unblock_threads(Semaphore* semaphore)
	{
		Kernel::CriticalScope critical;

		for (auto it = m_blocking_threads.begin(); it != m_blocking_threads.end();)
		{
			if (it->semaphore == semaphore)
			{
				auto thread = it->thread;
				it = m_blocking_threads.remove(it);

				// This should work as we released enough memory from active thread
				static_assert(sizeof(ActiveThread) == sizeof(BlockingThread));
				MUST(m_active_threads.emplace_back(thread));
			}
			else
			{
				it++;
			}
		}

		semaphore->m_blocked = false;
	}

}