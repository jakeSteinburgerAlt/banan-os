#pragma once

#include <BAN/LinkedList.h>
#include <BAN/Optional.h>
#include <BAN/UniqPtr.h>
#include <kernel/Memory/Heap.h>
#include <kernel/Memory/PageTable.h>

namespace Kernel
{

	class GeneralAllocator
	{
		BAN_NON_COPYABLE(GeneralAllocator);
		BAN_NON_MOVABLE(GeneralAllocator);

	public:
		static BAN::ErrorOr<BAN::UniqPtr<GeneralAllocator>> create(PageTable&, vaddr_t first_vaddr);
		~GeneralAllocator();

		BAN::ErrorOr<BAN::UniqPtr<GeneralAllocator>> clone(PageTable&);

		BAN::Optional<paddr_t> paddr_of(vaddr_t);

		vaddr_t allocate(size_t);
		bool deallocate(vaddr_t);

	private:
		GeneralAllocator(PageTable&, vaddr_t first_vaddr);

	private:
		struct Allocation
		{
			vaddr_t address { 0 };
			BAN::Vector<paddr_t> pages;

			bool contains(vaddr_t);
		};

	private:
		PageTable& m_page_table;
		BAN::LinkedList<Allocation> m_allocations;
		const vaddr_t m_first_vaddr;
	};

}