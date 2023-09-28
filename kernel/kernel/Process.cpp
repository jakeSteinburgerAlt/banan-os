#include <BAN/ScopeGuard.h>
#include <BAN/StringView.h>
#include <kernel/CriticalScope.h>
#include <kernel/FS/DevFS/FileSystem.h>
#include <kernel/FS/VirtualFileSystem.h>
#include <kernel/IDT.h>
#include <kernel/InterruptController.h>
#include <kernel/LockGuard.h>
#include <kernel/Memory/Heap.h>
#include <kernel/Memory/PageTableScope.h>
#include <kernel/Process.h>
#include <kernel/Scheduler.h>
#include <kernel/Storage/StorageDevice.h>
#include <kernel/Timer/Timer.h>
#include <LibELF/ELF.h>
#include <LibELF/Values.h>

#include <lai/helpers/pm.h>

#include <fcntl.h>
#include <stdio.h>
#include <sys/banan-os.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>

namespace Kernel
{

	static BAN::Vector<Process*> s_processes;
	static RecursiveSpinLock s_process_lock;

	void Process::for_each_process(const BAN::Function<BAN::Iteration(Process&)>& callback)
	{
		LockGuard _(s_process_lock);

		for (auto* process : s_processes)
		{
			auto ret = callback(*process);
			if (ret == BAN::Iteration::Break)
				return;
			ASSERT(ret == BAN::Iteration::Continue);
		}
	}

	void Process::for_each_process_in_session(pid_t sid, const BAN::Function<BAN::Iteration(Process&)>& callback)
	{
		LockGuard _(s_process_lock);

		for (auto* process : s_processes)
		{
			if (process->sid() != sid)
				continue;
			auto ret = callback(*process);
			if (ret == BAN::Iteration::Break)
				return;
			ASSERT(ret == BAN::Iteration::Continue);
		}
	}

	Process* Process::create_process(const Credentials& credentials, pid_t parent, pid_t sid, pid_t pgrp)
	{
		static pid_t s_next_id = 1;

		pid_t pid;
		{
			CriticalScope _;
			pid = s_next_id;
			if (sid == 0 && pgrp == 0)
			{
				sid = s_next_id;
				pgrp = s_next_id;
			}
			s_next_id++;
		}

		ASSERT(sid > 0);
		ASSERT(pgrp > 0);

		auto* process = new Process(credentials, pid, parent, sid, pgrp);
		ASSERT(process);

		return process;
	}

	void Process::register_to_scheduler()
	{
		s_process_lock.lock();
		MUST(s_processes.push_back(this));
		s_process_lock.unlock();
		for (auto* thread : m_threads)
			MUST(Scheduler::get().add_thread(thread));
	}

	Process* Process::create_kernel()
	{
		auto* process = create_process({ 0, 0, 0, 0 }, 0);
		MUST(process->m_working_directory.push_back('/'));
		return process;
	}

	Process* Process::create_kernel(entry_t entry, void* data)
	{
		auto* process = create_process({ 0, 0, 0, 0 }, 0);
		MUST(process->m_working_directory.push_back('/'));
		auto* thread = MUST(Thread::create_kernel(entry, data, process));
		process->add_thread(thread);
		process->register_to_scheduler();
		return process;
	}

	BAN::ErrorOr<Process*> Process::create_userspace(const Credentials& credentials, BAN::StringView path)
	{
		auto elf = TRY(load_elf_for_exec(credentials, path, "/"sv));

		auto* process = create_process(credentials, 0);
		MUST(process->m_working_directory.push_back('/'));
		process->m_page_table = BAN::UniqPtr<PageTable>::adopt(MUST(PageTable::create_userspace()));;

		process->load_elf_to_memory(*elf);

		process->m_is_userspace = true;
		process->m_userspace_info.entry = elf->file_header_native().e_entry;

		// NOTE: we clear the elf since we don't need the memory anymore
		elf.clear();

		char** argv = nullptr;
		{
			PageTableScope _(process->page_table());

			size_t needed_bytes = sizeof(char*) * 2 + path.size() + 1;
			if (auto rem = needed_bytes % PAGE_SIZE)
				needed_bytes += PAGE_SIZE - rem;

			auto argv_range = MUST(VirtualRange::create_to_vaddr_range(
				process->page_table(),
				0x400000, KERNEL_OFFSET,
				needed_bytes,
				PageTable::Flags::UserSupervisor | PageTable::Flags::ReadWrite | PageTable::Flags::Present
			));
			argv_range->set_zero();

			uintptr_t temp = argv_range->vaddr() + sizeof(char*) * 2;
			argv_range->copy_from(0, (uint8_t*)&temp, sizeof(char*));			

			temp = 0;
			argv_range->copy_from(sizeof(char*), (uint8_t*)&temp, sizeof(char*));	

			argv_range->copy_from(sizeof(char*) * 2, (const uint8_t*)path.data(), path.size());

			MUST(process->m_mapped_ranges.emplace_back(false, BAN::move(argv_range)));
		}

		process->m_userspace_info.argc = 1;
		process->m_userspace_info.argv = argv;
		process->m_userspace_info.envp = nullptr;

		auto* thread = MUST(Thread::create_userspace(process));
		process->add_thread(thread);
		process->register_to_scheduler();
		return process;
	}

	Process::Process(const Credentials& credentials, pid_t pid, pid_t parent, pid_t sid, pid_t pgrp)
		: m_credentials(credentials)
		, m_open_file_descriptors(m_credentials)
		, m_sid(sid)
		, m_pgrp(pgrp)
		, m_pid(pid)
		, m_parent(parent)
	{
		for (size_t i = 0; i < sizeof(m_signal_handlers) / sizeof(*m_signal_handlers); i++)
			m_signal_handlers[i] = (vaddr_t)SIG_DFL;
	}

	Process::~Process()
	{
		ASSERT(m_threads.empty());
		ASSERT(m_mapped_ranges.empty());
		ASSERT(m_exit_status.waiting == 0);
		ASSERT(&PageTable::current() != m_page_table.ptr());
	}

	void Process::add_thread(Thread* thread)
	{
		LockGuard _(m_lock);
		MUST(m_threads.push_back(thread));
	}

	void Process::cleanup_function()
	{
		s_process_lock.lock();
		for (size_t i = 0; i < s_processes.size(); i++)
			if (s_processes[i] == this)
				s_processes.remove(i);
		s_process_lock.unlock();

		m_lock.lock();
		m_exit_status.exited = true;
		while (m_exit_status.waiting > 0)
		{
			m_exit_status.semaphore.unblock();
			while (m_lock.is_locked())
				m_lock.unlock();
			Scheduler::get().reschedule();
			m_lock.lock();
		}

		m_open_file_descriptors.close_all();

		// NOTE: We must unmap ranges while the page table is still alive
		m_mapped_ranges.clear();
	}

	void Process::on_thread_exit(Thread& thread)
	{
		ASSERT(!interrupts_enabled());

		ASSERT(m_threads.size() > 0);

		if (m_threads.size() == 1)
		{
			ASSERT(m_threads.front() == &thread);
			m_threads.clear();

			thread.setup_process_cleanup();
			Scheduler::get().execute_current_thread();
		}

		for (size_t i = 0; i < m_threads.size(); i++)
		{
			if (m_threads[i] == &thread)
			{
				m_threads.remove(i);
				return;
			}
		}

		ASSERT_NOT_REACHED();
	}

	void Process::exit(int status, int signal)
	{
		LockGuard _(m_lock);
		m_exit_status.exit_code = __WGENEXITCODE(status, signal);
		for (auto* thread : m_threads)
			thread->set_terminating();
	}

	BAN::ErrorOr<long> Process::sys_exit(int status)
	{
		exit(status, 0);
		Thread::TerminateBlocker _(Thread::current());
		ASSERT_NOT_REACHED();
	}

	BAN::ErrorOr<long> Process::sys_gettermios(::termios* termios)
	{
		LockGuard _(m_lock);

		validate_pointer_access(termios, sizeof(::termios));

		if (!m_controlling_terminal)
			return BAN::Error::from_errno(ENOTTY);
		
		Kernel::termios ktermios = m_controlling_terminal->get_termios();
		termios->c_lflag = 0;
		if (ktermios.canonical)
			termios->c_lflag |= ICANON;
		if (ktermios.echo)
			termios->c_lflag |= ECHO;

		return 0;
	}

	BAN::ErrorOr<long> Process::sys_settermios(const ::termios* termios)
	{
		LockGuard _(m_lock);

		validate_pointer_access(termios, sizeof(::termios));

		if (!m_controlling_terminal)
			return BAN::Error::from_errno(ENOTTY);
		
		Kernel::termios ktermios;
		ktermios.echo = termios->c_lflag & ECHO;
		ktermios.canonical = termios->c_lflag & ICANON;
		
		m_controlling_terminal->set_termios(ktermios);
		return 0;
	}

	BAN::ErrorOr<BAN::UniqPtr<LibELF::ELF>> Process::load_elf_for_exec(const Credentials& credentials, BAN::StringView file_path, const BAN::String& cwd)
	{
		if (file_path.empty())
			return BAN::Error::from_errno(ENOENT);

		BAN::String absolute_path;

		if (file_path.front() == '/')
			TRY(absolute_path.append(file_path));
		else
		{
			TRY(absolute_path.append(cwd));
			TRY(absolute_path.push_back('/'));
			TRY(absolute_path.append(file_path));
		}

		auto file = TRY(VirtualFileSystem::get().file_from_absolute_path(credentials, absolute_path, O_EXEC));

		auto elf_or_error = LibELF::ELF::load_from_file(file.inode);
		if (elf_or_error.is_error())
		{
			if (elf_or_error.error().get_error_code() == EINVAL)
				return BAN::Error::from_errno(ENOEXEC);
			return elf_or_error.error();
		}
		
		auto elf = elf_or_error.release_value();
		if (!elf->is_native())
		{
			derrorln("ELF has invalid architecture");
			return BAN::Error::from_errno(EINVAL);
		}

		if (elf->file_header_native().e_type != LibELF::ET_EXEC)
		{
			derrorln("Not an executable");
			return BAN::Error::from_errno(ENOEXEC);
		}

		return BAN::move(elf);
	}

	BAN::ErrorOr<long> Process::sys_fork(uintptr_t rsp, uintptr_t rip)
	{
		auto page_table = BAN::UniqPtr<PageTable>::adopt(TRY(PageTable::create_userspace()));

		LockGuard _(m_lock);

		BAN::String working_directory;
		TRY(working_directory.append(m_working_directory));

		OpenFileDescriptorSet open_file_descriptors(m_credentials);
		TRY(open_file_descriptors.clone_from(m_open_file_descriptors));

		BAN::Vector<MappedRange> mapped_ranges;
		TRY(mapped_ranges.reserve(m_mapped_ranges.size()));
		for (auto& mapped_range : m_mapped_ranges)
			MUST(mapped_ranges.emplace_back(mapped_range.can_be_unmapped, TRY(mapped_range.range->clone(*page_table))));

		Process* forked = create_process(m_credentials, m_pid, m_sid, m_pgrp);
		forked->m_controlling_terminal = m_controlling_terminal;
		forked->m_working_directory = BAN::move(working_directory);
		forked->m_page_table = BAN::move(page_table);
		forked->m_open_file_descriptors = BAN::move(open_file_descriptors);
		forked->m_mapped_ranges = BAN::move(mapped_ranges);
		forked->m_is_userspace = m_is_userspace;
		forked->m_userspace_info = m_userspace_info;
		forked->m_has_called_exec = false;
		memcpy(forked->m_signal_handlers, m_signal_handlers, sizeof(m_signal_handlers));

		ASSERT(this == &Process::current());
		// FIXME: this should be able to fail
		Thread* thread = MUST(Thread::current().clone(forked, rsp, rip));
		forked->add_thread(thread);
		forked->register_to_scheduler();

		return forked->pid();
	}

	BAN::ErrorOr<long> Process::sys_exec(BAN::StringView path, const char* const* argv, const char* const* envp)
	{
		// NOTE: We scope everything for automatic deletion
		{
			BAN::Vector<BAN::String> str_argv;
			BAN::Vector<BAN::String> str_envp;

			{
				LockGuard _(m_lock);

				for (int i = 0; argv && argv[i]; i++)
				{
					validate_pointer_access(argv + i, sizeof(char*));
					validate_string_access(argv[i]);
					TRY(str_argv.emplace_back(argv[i]));
				}

				for (int i = 0; envp && envp[i]; i++)
				{
					validate_pointer_access(envp + 1, sizeof(char*));
					validate_string_access(envp[i]);
					TRY(str_envp.emplace_back(envp[i]));
				}
			}

			BAN::String working_directory;

			{
				LockGuard _(m_lock);
				TRY(working_directory.append(m_working_directory));
			}

			auto elf = TRY(load_elf_for_exec(m_credentials, path, working_directory));

			LockGuard lock_guard(m_lock);

			m_open_file_descriptors.close_cloexec();

			m_mapped_ranges.clear();

			load_elf_to_memory(*elf);

			m_userspace_info.entry = elf->file_header_native().e_entry;

			for (size_t i = 0; i < sizeof(m_signal_handlers) / sizeof(*m_signal_handlers); i++)
				m_signal_handlers[i] = (vaddr_t)SIG_DFL;

			// NOTE: we clear the elf since we don't need the memory anymore
			elf.clear();

			ASSERT(m_threads.size() == 1);
			ASSERT(&Process::current() == this);

			// allocate memory on the new process for arguments and environment
			auto create_range =
				[&](const auto& container) -> BAN::UniqPtr<VirtualRange>
				{
					size_t bytes = sizeof(char*);
					for (auto& elem : container)
						bytes += sizeof(char*) + elem.size() + 1;

					if (auto rem = bytes % PAGE_SIZE)
						bytes += PAGE_SIZE - rem;

					auto range = MUST(VirtualRange::create_to_vaddr_range(
						page_table(),
						0x400000, KERNEL_OFFSET,
						bytes,
						PageTable::Flags::UserSupervisor | PageTable::Flags::ReadWrite | PageTable::Flags::Present
					));
					range->set_zero();

					size_t data_offset = sizeof(char*) * (container.size() + 1);
					for (size_t i = 0; i < container.size(); i++)
					{
						uintptr_t ptr_addr = range->vaddr() + data_offset;
						range->copy_from(sizeof(char*) * i, (const uint8_t*)&ptr_addr, sizeof(char*));
						range->copy_from(data_offset, (const uint8_t*)container[i].data(), container[i].size());
						data_offset += container[i].size() + 1;
					}

					uintptr_t null = 0;
					range->copy_from(sizeof(char*) * container.size(), (const uint8_t*)&null, sizeof(char*));

					return BAN::move(range);
				};

			auto argv_range = create_range(str_argv);
			m_userspace_info.argv = (char**)argv_range->vaddr();
			MUST(m_mapped_ranges.emplace_back(false, BAN::move(argv_range)));

			auto envp_range = create_range(str_envp);
			m_userspace_info.envp = (char**)envp_range->vaddr();
			MUST(m_mapped_ranges.emplace_back(false, BAN::move(envp_range)));

			m_userspace_info.argc = str_argv.size();

			asm volatile("cli");
		}

		m_threads.front()->setup_exec();
		Scheduler::get().execute_current_thread();
		ASSERT_NOT_REACHED();
	}

	int Process::block_until_exit()
	{
		ASSERT(this != &Process::current());

		m_lock.lock();
		m_exit_status.waiting++;
		while (!m_exit_status.exited)
		{
			m_lock.unlock();
			m_exit_status.semaphore.block();
			m_lock.lock();
		}

		int ret = m_exit_status.exit_code;
		m_exit_status.waiting--;
		m_lock.unlock();

		return ret;
	}

	BAN::ErrorOr<long> Process::sys_wait(pid_t pid, int* stat_loc, int options)
	{
		Process* target = nullptr;

		{
			LockGuard _(m_lock);
			validate_pointer_access(stat_loc, sizeof(int));
		}

		// FIXME: support options
		if (options)
			return BAN::Error::from_errno(EINVAL);

		for_each_process(
			[&](Process& process)
			{
				if (process.pid() == pid)
				{
					target = &process;
					return BAN::Iteration::Break;
				}
				return BAN::Iteration::Continue;
			}
		);

		if (target == nullptr)
			return BAN::Error::from_errno(ECHILD);

		pid_t ret = target->pid();
		*stat_loc = target->block_until_exit();

		return ret;
	}

	BAN::ErrorOr<long> Process::sys_sleep(int seconds)
	{
		SystemTimer::get().sleep(seconds * 1000);
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_nanosleep(const timespec* rqtp, timespec* rmtp)
	{
		{
			LockGuard _(m_lock);
			validate_pointer_access(rqtp, sizeof(timespec));
			validate_pointer_access(rmtp, sizeof(timespec));
		}
		// TODO: rmtp
		SystemTimer::get().sleep(rqtp->tv_sec * 1000 + BAN::Math::div_round_up<uint64_t>(rqtp->tv_nsec, 1'000'000));
		return 0;
	}

	void Process::load_elf_to_memory(LibELF::ELF& elf)
	{
		ASSERT(elf.is_native());

		auto& elf_file_header = elf.file_header_native();
		for (size_t i = 0; i < elf_file_header.e_phnum; i++)
		{
			auto& elf_program_header = elf.program_header_native(i);

			switch (elf_program_header.p_type)
			{
			case LibELF::PT_NULL:
				break;
			case LibELF::PT_LOAD:
			{
				PageTable::flags_t flags = PageTable::Flags::UserSupervisor | PageTable::Flags::Present;
				if (elf_program_header.p_flags & LibELF::PF_W)
					flags |= PageTable::Flags::ReadWrite;
				if (elf_program_header.p_flags & LibELF::PF_X)
					flags |= PageTable::Flags::Execute;

				size_t page_start = elf_program_header.p_vaddr / PAGE_SIZE;
				size_t page_end = BAN::Math::div_round_up<size_t>(elf_program_header.p_vaddr + elf_program_header.p_memsz, PAGE_SIZE);
				size_t page_count = page_end - page_start;

				page_table().lock();

				if (!page_table().is_range_free(page_start * PAGE_SIZE, page_count * PAGE_SIZE))
				{
					page_table().debug_dump();
					Kernel::panic("vaddr {8H}-{8H} not free {8H}-{8H}",
						page_start * PAGE_SIZE,
						page_start * PAGE_SIZE + page_count * PAGE_SIZE
					);
				}

				{
					LockGuard _(m_lock);
					auto range = MUST(VirtualRange::create_to_vaddr(page_table(), page_start * PAGE_SIZE, page_count * PAGE_SIZE, flags));
					range->set_zero();
					range->copy_from(elf_program_header.p_vaddr % PAGE_SIZE, elf.data() + elf_program_header.p_offset, elf_program_header.p_filesz);

					MUST(m_mapped_ranges.emplace_back(false, BAN::move(range)));
				}

				page_table().unlock();

				break;
			}
			default:
				ASSERT_NOT_REACHED();
			}
		}

		m_has_called_exec = true;
	}

	BAN::ErrorOr<void> Process::create_file(BAN::StringView path, mode_t mode)
	{
		LockGuard _(m_lock);

		auto absolute_path = TRY(absolute_path_of(path));

		size_t index;
		for (index = absolute_path.size(); index > 0; index--)
			if (absolute_path[index - 1] == '/')
				break;

		auto directory = absolute_path.sv().substring(0, index);
		auto file_name = absolute_path.sv().substring(index);

		auto parent_inode = TRY(VirtualFileSystem::get().file_from_absolute_path(m_credentials, directory, O_WRONLY)).inode;
		TRY(parent_inode->create_file(file_name, S_IFREG | (mode & 0777), m_credentials.euid(), m_credentials.egid()));

		return {};
	}

	BAN::ErrorOr<long> Process::open_file(BAN::StringView path, int flags, mode_t mode)
	{
		BAN::String absolute_path = TRY(absolute_path_of(path));

		if (flags & O_CREAT)
		{
			if (flags & O_DIRECTORY)
				return BAN::Error::from_errno(ENOTSUP);
			auto file_or_error = VirtualFileSystem::get().file_from_absolute_path(m_credentials, absolute_path, O_WRONLY);
			if (file_or_error.is_error())
			{
				if (file_or_error.error().get_error_code() == ENOENT)
					TRY(create_file(path, mode));
				else
					return file_or_error.release_error();
			}
			flags &= ~O_CREAT;
		}

		int fd = TRY(m_open_file_descriptors.open(absolute_path, flags));
		auto inode = MUST(m_open_file_descriptors.inode_of(fd));

		// Open controlling terminal
		if ((flags & O_TTY_INIT) && !(flags & O_NOCTTY) && inode->is_tty() && is_session_leader() && !m_controlling_terminal)
			m_controlling_terminal = (TTY*)inode.ptr();

		return fd;
	}

	BAN::ErrorOr<long> Process::sys_open(const char* path, int flags, mode_t mode)
	{
		LockGuard _(m_lock);
		validate_string_access(path);
		return open_file(path, flags, mode);
	}

	BAN::ErrorOr<long> Process::sys_openat(int fd, const char* path, int flags, mode_t mode)
	{
		LockGuard _(m_lock);

		validate_string_access(path);

		// FIXME: handle O_SEARCH in fd

		BAN::String absolute_path;
		TRY(absolute_path.append(TRY(m_open_file_descriptors.path_of(fd))));
		TRY(absolute_path.push_back('/'));
		TRY(absolute_path.append(path));

		return open_file(absolute_path, flags, mode);
	}

	BAN::ErrorOr<long> Process::sys_close(int fd)
	{
		LockGuard _(m_lock);
		TRY(m_open_file_descriptors.close(fd));
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_read(int fd, void* buffer, size_t count)
	{
		LockGuard _(m_lock);
		validate_pointer_access(buffer, count);
		return TRY(m_open_file_descriptors.read(fd, buffer, count));
	}

	BAN::ErrorOr<long> Process::sys_write(int fd, const void* buffer, size_t count)
	{
		LockGuard _(m_lock);
		validate_pointer_access(buffer, count);
		return TRY(m_open_file_descriptors.write(fd, buffer, count));
	}

	BAN::ErrorOr<long> Process::sys_pipe(int fildes[2])
	{
		LockGuard _(m_lock);
		validate_pointer_access(fildes, sizeof(int) * 2);
		TRY(m_open_file_descriptors.pipe(fildes));
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_dup(int fildes)
	{
		LockGuard _(m_lock);
		return TRY(m_open_file_descriptors.dup(fildes));
	}

	BAN::ErrorOr<long> Process::sys_dup2(int fildes, int fildes2)
	{
		LockGuard _(m_lock);
		return TRY(m_open_file_descriptors.dup2(fildes, fildes2));
	}

	BAN::ErrorOr<long> Process::sys_fcntl(int fildes, int cmd, int extra)
	{
		LockGuard _(m_lock);
		return TRY(m_open_file_descriptors.fcntl(fildes, cmd, extra));
	}

	BAN::ErrorOr<long> Process::sys_seek(int fd, off_t offset, int whence)
	{
		LockGuard _(m_lock);
		TRY(m_open_file_descriptors.seek(fd, offset, whence));
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_tell(int fd)
	{
		LockGuard _(m_lock);
		return TRY(m_open_file_descriptors.tell(fd));
	}

	BAN::ErrorOr<void> Process::mount(BAN::StringView source, BAN::StringView target)
	{
		BAN::String absolute_source, absolute_target;
		{
			LockGuard _(m_lock);
			TRY(absolute_source.append(TRY(absolute_path_of(source))));
			TRY(absolute_target.append(TRY(absolute_path_of(target))));
		}
		TRY(VirtualFileSystem::get().mount(m_credentials, absolute_source, absolute_target));
		return {};
	}

	BAN::ErrorOr<long> Process::sys_fstat(int fd, struct stat* buf)
	{
		LockGuard _(m_lock);
		validate_pointer_access(buf, sizeof(struct stat));
		TRY(m_open_file_descriptors.fstat(fd, buf));
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_fstatat(int fd, const char* path, struct stat* buf, int flag)
	{
		LockGuard _(m_lock);
		validate_pointer_access(buf, sizeof(struct stat));
		TRY(m_open_file_descriptors.fstatat(fd, path, buf, flag));
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_stat(const char* path, struct stat* buf, int flag)
	{
		LockGuard _(m_lock);
		validate_pointer_access(buf, sizeof(struct stat));
		TRY(m_open_file_descriptors.stat(TRY(absolute_path_of(path)), buf, flag));
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_sync(bool should_block)
	{
		DevFileSystem::get().initiate_sync(should_block);
		return 0;
	}

	[[noreturn]] static void reset_system()
	{
		lai_acpi_reset();

		// acpi reset did not work

		dwarnln("Could not reset with ACPI, crashing the cpu");

		// reset through triple fault
		IDT::force_triple_fault();
	}

	BAN::ErrorOr<long> Process::sys_poweroff(int command)
	{
		if (command != POWEROFF_REBOOT && command != POWEROFF_SHUTDOWN)
			return BAN::Error::from_errno(EINVAL);

		// FIXME: gracefully kill all processes

		DevFileSystem::get().initiate_sync(true);
		
		lai_api_error_t error;
		switch (command)
		{
			case POWEROFF_REBOOT:
				reset_system();
				break;
			case POWEROFF_SHUTDOWN:
				error = lai_enter_sleep(5);
				break;
			default:
				ASSERT_NOT_REACHED();
		}
		
		// If we reach here, there was an error
		dprintln("{}", lai_api_error_to_string(error));
		return BAN::Error::from_errno(EUNKNOWN);
	}

	BAN::ErrorOr<long> Process::sys_read_dir_entries(int fd, DirectoryEntryList* list, size_t list_size)
	{
		LockGuard _(m_lock);
		validate_pointer_access(list, list_size);
		TRY(m_open_file_descriptors.read_dir_entries(fd, list, list_size));
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_setpwd(const char* path)
	{
		BAN::String absolute_path;

		{
			LockGuard _(m_lock);
			validate_string_access(path);
			absolute_path = TRY(absolute_path_of(path));
		}

		auto file = TRY(VirtualFileSystem::get().file_from_absolute_path(m_credentials, absolute_path, O_SEARCH));
		if (!file.inode->mode().ifdir())
			return BAN::Error::from_errno(ENOTDIR);

		LockGuard _(m_lock);
		m_working_directory = BAN::move(file.canonical_path);

		return 0;
	}

	BAN::ErrorOr<long> Process::sys_getpwd(char* buffer, size_t size)
	{
		LockGuard _(m_lock);

		validate_pointer_access(buffer, size);

		if (size < m_working_directory.size() + 1)
			return BAN::Error::from_errno(ERANGE);
		
		memcpy(buffer, m_working_directory.data(), m_working_directory.size());
		buffer[m_working_directory.size()] = '\0';

		return (long)buffer;
	}

	BAN::ErrorOr<long> Process::sys_mmap(const sys_mmap_t* args)
	{
		{
			LockGuard _(m_lock);
			validate_pointer_access(args, sizeof(sys_mmap_t));
		}

		if (args->prot != PROT_NONE && args->prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
			return BAN::Error::from_errno(EINVAL);

		PageTable::flags_t flags = PageTable::Flags::UserSupervisor;
		if (args->prot & PROT_READ)
			flags |= PageTable::Flags::Present;
		if (args->prot & PROT_WRITE)
			flags |= PageTable::Flags::ReadWrite | PageTable::Flags::Present;
		if (args->prot & PROT_EXEC)
			flags |= PageTable::Flags::Execute | PageTable::Flags::Present;

		if (args->flags == (MAP_ANONYMOUS | MAP_PRIVATE))
		{
			if (args->addr != nullptr)
				return BAN::Error::from_errno(ENOTSUP);
			if (args->off != 0)
				return BAN::Error::from_errno(EINVAL);
			if (args->len % PAGE_SIZE != 0)
				return BAN::Error::from_errno(EINVAL);

			auto range = TRY(VirtualRange::create_to_vaddr_range(
				page_table(),
				0x400000, KERNEL_OFFSET,
				args->len,
				PageTable::Flags::UserSupervisor | PageTable::Flags::ReadWrite | PageTable::Flags::Present
			));
			range->set_zero();

			LockGuard _(m_lock);
			TRY(m_mapped_ranges.emplace_back(true, BAN::move(range)));
			return m_mapped_ranges.back().range->vaddr();
		}

		return BAN::Error::from_errno(ENOTSUP);
	}

	BAN::ErrorOr<long> Process::sys_munmap(void* addr, size_t len)
	{
		if (len == 0)
			return BAN::Error::from_errno(EINVAL);

		vaddr_t vaddr = (vaddr_t)addr;
		if (vaddr % PAGE_SIZE != 0)
			return BAN::Error::from_errno(EINVAL);

		LockGuard _(m_lock);

		for (size_t i = 0; i < m_mapped_ranges.size(); i++)
		{
			if (!m_mapped_ranges[i].can_be_unmapped)
				continue;
			auto& range = m_mapped_ranges[i].range;
			if (vaddr + len < range->vaddr() || vaddr >= range->vaddr() + range->size())
				continue;
			m_mapped_ranges.remove(i);
		}

		return 0;
	}

	BAN::ErrorOr<long> Process::sys_tty_ctrl(int fildes, int command, int flags)
	{
		LockGuard _(m_lock);

		auto inode = TRY(m_open_file_descriptors.inode_of(fildes));
		if (!inode->is_tty())
			return BAN::Error::from_errno(ENOTTY);
		
		TRY(((TTY*)inode.ptr())->tty_ctrl(command, flags));

		return 0;
	}

	BAN::ErrorOr<long> Process::sys_termid(char* buffer)
	{
		LockGuard _(m_lock);

		validate_string_access(buffer);

		auto& tty = m_controlling_terminal;

		if (!tty)
			buffer[0] = '\0';
		else
		{
			ASSERT(minor(tty->rdev()) < 10);
			strcpy(buffer, "/dev/tty0");
			buffer[8] += minor(tty->rdev());
		}

		return 0;
	}

	BAN::ErrorOr<long> Process::sys_clock_gettime(clockid_t clock_id, timespec* tp)
	{
		{
			LockGuard _(m_lock);
			validate_pointer_access(tp, sizeof(timespec));
		}

		switch (clock_id)
		{
			case CLOCK_MONOTONIC:
			{
				*tp = SystemTimer::get().time_since_boot();
				break;
			}
			case CLOCK_REALTIME:
			{
				*tp = SystemTimer::get().real_time();
				break;
			}
			default:
				return BAN::Error::from_errno(ENOTSUP);
		}
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_signal(int signal, void (*handler)(int))
	{
		if (signal < _SIGMIN || signal > _SIGMAX)
			return BAN::Error::from_errno(EINVAL);

		{
			LockGuard _(m_lock);
			validate_pointer_access((void*)handler, sizeof(handler));
		}

		CriticalScope _;
		m_signal_handlers[signal] = (vaddr_t)handler;
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_kill(pid_t pid, int signal)
	{
		if (pid == 0 || pid == -1)
			return BAN::Error::from_errno(ENOTSUP);
		if (signal != 0 && (signal < _SIGMIN || signal > _SIGMAX))
			return BAN::Error::from_errno(EINVAL);

		if (pid == Process::current().pid())
			return Process::current().sys_raise(signal);
		
		bool found = false;
		for_each_process(
			[&](Process& process)
			{
				if (pid == process.pid() || -pid == process.pgrp())
				{
					found = true;
					if (signal)
					{
						CriticalScope _;
						process.m_signal_pending_mask |= 1 << signal;
					}
					return (pid > 0) ? BAN::Iteration::Break : BAN::Iteration::Continue;
				}
				return BAN::Iteration::Continue;
			}
		);

		if (found)
			return 0;
		return BAN::Error::from_errno(ESRCH);
	}

	BAN::ErrorOr<long> Process::sys_raise(int signal)
	{
		if (signal < _SIGMIN || signal > _SIGMAX)
			return BAN::Error::from_errno(EINVAL);
		ASSERT(this == &Process::current());
		
		CriticalScope _;
		Thread::current().handle_signal(signal);
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_tcsetpgrp(int fd, pid_t pgrp)
	{
		LockGuard _(m_lock);

		if (!m_controlling_terminal)
			return BAN::Error::from_errno(ENOTTY);

		bool valid_pgrp = false;
		for_each_process(
			[&](Process& process)
			{
				if (process.sid() == sid() && process.pgrp() == pgrp)
				{
					valid_pgrp = true;
					return BAN::Iteration::Break;
				}
				return BAN::Iteration::Continue;
			}
		);
		if (!valid_pgrp)
			return BAN::Error::from_errno(EPERM);

		auto inode = TRY(m_open_file_descriptors.inode_of(fd));
		if (!inode->is_tty())
			return BAN::Error::from_errno(ENOTTY);

		if ((TTY*)inode.ptr() != m_controlling_terminal.ptr())
			return BAN::Error::from_errno(ENOTTY);

		((TTY*)inode.ptr())->set_foreground_pgrp(pgrp);
		return 0;
	}

	BAN::ErrorOr<long> Process::sys_setuid(uid_t uid)
	{
		if (uid < 0 || uid >= 1'000'000'000)
			return BAN::Error::from_errno(EINVAL);

		LockGuard _(m_lock);

		// If the process has appropriate privileges, setuid() shall set the real user ID, effective user ID, and the saved
		// set-user-ID of the calling process to uid.
		if (m_credentials.is_superuser())
		{
			m_credentials.set_euid(uid);
			m_credentials.set_ruid(uid);
			m_credentials.set_suid(uid);
			return 0;
		}

		// If the process does not have appropriate privileges, but uid is equal to the real user ID or the saved set-user-ID,
		// setuid() shall set the effective user ID to uid; the real user ID and saved set-user-ID shall remain unchanged.
		if (uid == m_credentials.ruid() || uid == m_credentials.suid())
		{
			m_credentials.set_euid(uid);
			return 0;
		}

		return BAN::Error::from_errno(EPERM);
	}

	BAN::ErrorOr<long> Process::sys_setgid(gid_t gid)
	{
		if (gid < 0 || gid >= 1'000'000'000)
			return BAN::Error::from_errno(EINVAL);

		LockGuard _(m_lock);

		// If the process has appropriate privileges, setgid() shall set the real group ID, effective group ID, and the saved
		// set-group-ID of the calling process to gid.
		if (m_credentials.is_superuser())
		{
			m_credentials.set_egid(gid);
			m_credentials.set_rgid(gid);
			m_credentials.set_sgid(gid);
			return 0;
		}

		// If the process does not have appropriate privileges, but gid is equal to the real group ID or the saved set-group-ID,
		// setgid() shall set the effective group ID to gid; the real group ID and saved set-group-ID shall remain unchanged.
		if (gid == m_credentials.rgid() || gid == m_credentials.sgid())
		{
			m_credentials.set_egid(gid);
			return 0;
		}

		return BAN::Error::from_errno(EPERM);
	}

	BAN::ErrorOr<long> Process::sys_seteuid(uid_t uid)
	{
		if (uid < 0 || uid >= 1'000'000'000)
			return BAN::Error::from_errno(EINVAL);

		LockGuard _(m_lock);

		// If uid is equal to the real user ID or the saved set-user-ID, or if the process has appropriate privileges, seteuid()
		// shall set the effective user ID of the calling process to uid; the real user ID and saved set-user-ID shall remain unchanged.
		if (uid == m_credentials.ruid() || uid == m_credentials.suid() || m_credentials.is_superuser())
		{
			m_credentials.set_euid(uid);
			return 0;
		}

		return BAN::Error::from_errno(EPERM);
	}

	BAN::ErrorOr<long> Process::sys_setegid(gid_t gid)
	{
		if (gid < 0 || gid >= 1'000'000'000)
			return BAN::Error::from_errno(EINVAL);

		LockGuard _(m_lock);

		// If gid is equal to the real group ID or the saved set-group-ID, or if the process has appropriate privileges, setegid()
		// shall set the effective group ID of the calling process to gid; the real group ID, saved set-group-ID, and any
		// supplementary group IDs shall remain unchanged.
		if (gid == m_credentials.rgid() || gid == m_credentials.sgid() || m_credentials.is_superuser())
		{
			m_credentials.set_egid(gid);
			return 0;
		}

		return BAN::Error::from_errno(EPERM);
	}

	BAN::ErrorOr<long> Process::sys_setreuid(uid_t ruid, uid_t euid)
	{
		if (ruid == -1 && euid == -1)
			return 0;

		if (ruid < -1 || ruid >= 1'000'000'000)
			return BAN::Error::from_errno(EINVAL);
		if (euid < -1 || euid >= 1'000'000'000)
			return BAN::Error::from_errno(EINVAL);

		// The setreuid() function shall set the real and effective user IDs of the current process to the values specified
		// by the ruid and euid arguments. If ruid or euid is -1, the corresponding effective or real user ID of the current
		// process shall be left unchanged.

		LockGuard _(m_lock);

		// A process with appropriate privileges can set either ID to any value.
		if (!m_credentials.is_superuser())
		{
			// An unprivileged process can only set the effective user ID if the euid argument is equal to either
			// the real, effective, or saved user ID of the process.
			if (euid != -1 && euid != m_credentials.ruid() && euid != m_credentials.euid() && euid == m_credentials.suid())
				return BAN::Error::from_errno(EPERM);

			// It is unspecified whether a process without appropriate privileges is permitted to change the real user ID to match the
			// current effective user ID or saved set-user-ID of the process.
			// NOTE: we will allow this
			if (ruid != -1 && ruid != m_credentials.ruid() && ruid != m_credentials.euid() && ruid == m_credentials.suid())
				return BAN::Error::from_errno(EPERM);
		}

		// If the real user ID is being set (ruid is not -1), or the effective user ID is being set to a value not equal to the
		// real user ID, then the saved set-user-ID of the current process shall be set equal to the new effective user ID.
		if (ruid != -1 || euid != m_credentials.ruid())
			m_credentials.set_suid(euid);
		
		if (ruid != -1)
			m_credentials.set_ruid(ruid);
		if (euid != -1)
			m_credentials.set_euid(euid);

		return 0;
	}

	BAN::ErrorOr<long> Process::sys_setregid(gid_t rgid, gid_t egid)
	{
		if (rgid == -1 && egid == -1)
			return 0;

		if (rgid < -1 || rgid >= 1'000'000'000)
			return BAN::Error::from_errno(EINVAL);
		if (egid < -1 || egid >= 1'000'000'000)
			return BAN::Error::from_errno(EINVAL);

		// The setregid() function shall set the real and effective group IDs of the calling process.
		
		// If rgid is -1, the real group ID shall not be changed; if egid is -1, the effective group ID shall not be changed.

		// The real and effective group IDs may be set to different values in the same call.

		LockGuard _(m_lock);

		// Only a process with appropriate privileges can set the real group ID and the effective group ID to any valid value.
		if (!m_credentials.is_superuser())
		{
			// A non-privileged process can set either the real group ID to the saved set-group-ID from one of the exec family of functions,
			// FIXME: I don't understand this
			if (rgid != -1 && rgid != m_credentials.sgid())
				return BAN::Error::from_errno(EPERM);

			// or the effective group ID to the saved set-group-ID or the real group ID.
			if (egid != -1 && egid != m_credentials.sgid() && egid != m_credentials.rgid())
				return BAN::Error::from_errno(EPERM);
		}

		// If the real group ID is being set (rgid is not -1), or the effective group ID is being set to a value not equal to the
		// real group ID, then the saved set-group-ID of the current process shall be set equal to the new effective group ID.
		if (rgid != -1 || egid != m_credentials.rgid())
			m_credentials.set_sgid(egid);
		
		if (rgid != -1)
			m_credentials.set_rgid(rgid);
		if (egid != -1)
			m_credentials.set_egid(egid);

		return 0;
	}

	BAN::ErrorOr<long> Process::sys_setpgid(pid_t pid, pid_t pgid)
	{
		if (pgid < 0)
			return BAN::Error::from_errno(EINVAL);

		LockGuard _(m_lock);

		if (pid == 0)
			pid = m_pid;
		if (pgid == 0)
			pgid = m_pid;

		if (pid != pgid)
		{
			bool pgid_valid = false;
			for_each_process_in_session(m_sid,
				[&](Process& process)
				{
					if (process.pgrp() == pgid)
					{
						pgid_valid = true;
						return BAN::Iteration::Break;
					}
					return BAN::Iteration::Continue;
				}
			);
			if (!pgid_valid)
				return BAN::Error::from_errno(EPERM);
		}

		if (m_pid == pid)
		{
			if (is_session_leader())
				return BAN::Error::from_errno(EPERM);
			m_pgrp = pgid;
			return 0;
		}

		int error = ESRCH;
		for_each_process(
			[&](Process& process)
			{
				if (process.pid() != pid)
					return BAN::Iteration::Continue;

				if (process.m_parent != m_pid)
					error = ESRCH;
				else if (process.is_session_leader())
					error = EPERM;
				else if (process.m_has_called_exec)
					error = EACCES;
				else if (process.m_sid != m_sid)
					error = EPERM;
				else
				{
					error = 0;
					process.m_pgrp = pgid;
				}

				return BAN::Iteration::Break;
			}
		);

		if (error == 0)
			return 0;
		return BAN::Error::from_errno(error);
	}

	BAN::ErrorOr<long> Process::sys_getpgid(pid_t pid)
	{
		LockGuard _(m_lock);

		if (pid == 0 || pid == m_pid)
			return m_pgrp;

		pid_t result;
		int error = ESRCH;
		for_each_process(
			[&](Process& process)
			{
				if (process.pid() != pid)
					return BAN::Iteration::Continue;

				if (process.sid() != m_sid)
					error = EPERM;
				else
				{
					error = 0;
					result = process.pgrp();
				}

				return BAN::Iteration::Break;
			}
		);

		if (error == 0)
			return result;
		return BAN::Error::from_errno(error);
	}

	BAN::ErrorOr<BAN::String> Process::absolute_path_of(BAN::StringView path) const
	{
		ASSERT(m_lock.is_locked());

		if (path.empty() || path == "."sv)
			return m_working_directory;

		BAN::String absolute_path;
		if (path.front() != '/')
			TRY(absolute_path.append(m_working_directory));

		if (!absolute_path.empty() && absolute_path.back() != '/')
			TRY(absolute_path.push_back('/'));

		TRY(absolute_path.append(path));
		
		return absolute_path;
	}

	void Process::validate_string_access(const char* str)
	{
		// NOTE: we will page fault here, if str is not actually mapped
		//       outcome is still the same; SIGSEGV
		validate_pointer_access(str, strlen(str) + 1);
	}

	void Process::validate_pointer_access(const void* ptr, size_t size)
	{
		ASSERT(&Process::current() == this);
		auto& thread = Thread::current();

		vaddr_t vaddr = (vaddr_t)ptr;

		// NOTE: detect overflow
		if (vaddr + size < vaddr)
			goto unauthorized_access;

		// trying to access kernel space memory
		if (vaddr + size > KERNEL_OFFSET)
			goto unauthorized_access;

		if (vaddr == 0)
			return;

		if (vaddr >= thread.stack_base() && vaddr + size <= thread.stack_base() + thread.stack_size())
			return;

		// FIXME: should we allow cross mapping access?
		for (auto& mapped_range : m_mapped_ranges)
			if (vaddr >= mapped_range.range->vaddr() && vaddr + size <= mapped_range.range->vaddr() + mapped_range.range->size())
				return;

unauthorized_access:
		dwarnln("process {}, thread {} attempted to make an invalid pointer access", pid(), Thread::current().tid());
		Debug::dump_stack_trace();
		MUST(sys_raise(SIGSEGV));
	}

}