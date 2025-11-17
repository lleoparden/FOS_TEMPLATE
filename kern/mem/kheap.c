#include "kheap.h"

#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h>
#include "../conc/kspinlock.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 kheap_init [GIVEN]
// Remember to initialize locks (if any)

struct kspinlock kheap_lock;
void kheap_init()
{
	init_kspinlock(&kheap_lock, "kernel_heap");
	//==================================================================================
	// DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		initialize_dynamic_allocator(KERNEL_HEAP_START, KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE);
		set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
		kheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		kheapPageAllocBreak = kheapPageAllocStart;
	}
	//==================================================================================
	//==================================================================================
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void *va)
{
	int ret = alloc_page(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE), PERM_WRITEABLE, 1);
	if (ret < 0)
		panic("get_page() in kern: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void *va)
{
	unmap_frame(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE));
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
void *kmalloc(unsigned int size)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #1 kmalloc
	// Your code is here
	// Comment the following line
	//  kpanic_into_prompt("kmalloc() is not implemented yet...!!");

	if (size == 0)
	{
		return NULL;
	}

	acquire_kspinlock(&kheap_lock);

	if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	{
		release_kspinlock(&kheap_lock);
		return alloc_block(size);
	}

	size = ROUNDUP(size, PAGE_SIZE);

	uint32 worstfit_size = 0;
	uint32 worstfit_start = 0;
	uint32 ptr = kheapPageAllocStart;

	if (kheapPageAllocBreak == kheapPageAllocStart)
	{
		release_kspinlock(&kheap_lock);
		goto extend_heap;
	}

	while (ptr < kheapPageAllocBreak)
	{
		uint32 pde_val = ptr_page_directory[PDX(ptr)];
		uint32 *ptr_page_table = NULL;
		int gt_ret = TABLE_NOT_EXIST;
		struct FrameInfo *info = NULL;

		/*
		 * IMPORTANT SAFETY CHECK
		 * - If PDE has PERM_PRESENT -> it's safe to call get_page_table()
		 *   because get_page_table() will not call fault_handler() if the table is in memory.
		 * - If PDE == 0 or PDE has no PERM_PRESENT bit, DO NOT call get_page_table()
		 *   (calling it may cause lcr2/fault_handler and panic in kernel context).
		 */
		if (pde_val & PERM_PRESENT)
		{
			gt_ret = get_page_table(ptr_page_directory, ptr, &ptr_page_table);
			if (gt_ret == TABLE_IN_MEMORY && ptr_page_table != NULL)
			{
				info = get_frame_info(ptr_page_directory, ptr, &ptr_page_table);
			}
			else
			{
				info = NULL;
			}
		}
		else
		{
			info = NULL;
		}

		if (info != NULL)
		{
			if (info->is_start_of_alloc)
				ptr += info->allocation_size * PAGE_SIZE;
			else
				ptr += PAGE_SIZE;
		}
		else
		{
			uint32 fara8_start = ptr;
			uint32 fara8_size = 0;

			while (ptr < kheapPageAllocBreak)
			{
				pde_val = ptr_page_directory[PDX(ptr)];

				if (pde_val & PERM_PRESENT)
				{
					gt_ret = get_page_table(ptr_page_directory, ptr, &ptr_page_table);
					if (gt_ret == TABLE_IN_MEMORY && ptr_page_table != NULL)
					{
						struct FrameInfo *inner_info = get_frame_info(ptr_page_directory, ptr, &ptr_page_table);
						if (inner_info != NULL)
							break;
					}
				}

				ptr += PAGE_SIZE;
				fara8_size += PAGE_SIZE;
			}

			if (fara8_size == size)
			{
				uint32 num_of_pages = size / PAGE_SIZE;
				release_kspinlock(&kheap_lock);

				for (uint32 i = 0; i < num_of_pages; i++)
				{
					if (alloc_page(ptr_page_directory, fara8_start + i * PAGE_SIZE, PERM_WRITEABLE, 1) != 0)
						panic("kmalloc: Alloc_page failed in exact-fit loop!");

					struct FrameInfo *fi = get_frame_info(ptr_page_directory, fara8_start + i * PAGE_SIZE, NULL);
					if (fi == NULL)
						panic("kmalloc: NULL FRAME in exact-fit");

					fi->allocation_size = (i == 0) ? num_of_pages : 0;
					fi->is_start_of_alloc = (i == 0) ? 1 : 0;
				}
				return (void *)fara8_start;
			}
			else if (fara8_size > worstfit_size)
			{
				worstfit_size = fara8_size;
				worstfit_start = fara8_start;
			}
		}
	}

	if (worstfit_size >= size)
	{
		uint32 num_of_pages = size / PAGE_SIZE;
		release_kspinlock(&kheap_lock);
		for (uint32 i = 0; i < num_of_pages; i++)
		{
			if (alloc_page(ptr_page_directory, worstfit_start + (i * PAGE_SIZE), PERM_WRITEABLE, 1) != 0)
				panic("kmalloc: Alloc_page failed in worst-fit loop!");
			struct FrameInfo *fi = get_frame_info(ptr_page_directory, worstfit_start + (i * PAGE_SIZE), NULL);
			if (fi == NULL)
				panic("kmalloc: NULL FRAME in worst-fit");
			fi->allocation_size = (i == 0) ? num_of_pages : 0;
			fi->is_start_of_alloc = (i == 0) ? 1 : 0;
		}
		return (void *)worstfit_start;
	}

	release_kspinlock(&kheap_lock);

extend_heap:
{

	uint32 va;
	uint32 num_of_pages = size / PAGE_SIZE;

	acquire_kspinlock(&kheap_lock);
	if (kheapPageAllocBreak == 0)
	{
		release_kspinlock(&kheap_lock);
		panic("HEAP CORRUPTION: kheapPageAllocBreak is 0!");
	}

	if (size > (KERNEL_HEAP_MAX - kheapPageAllocBreak))
	{
		release_kspinlock(&kheap_lock);
		return NULL;
	}

	va = ROUNDUP(kheapPageAllocBreak, PAGE_SIZE);

	uint32 old_break = kheapPageAllocBreak;
	kheapPageAllocBreak = va + size;
	release_kspinlock(&kheap_lock);

	uint32 pages_allocated = 0;
	for (uint32 i = 0; i < num_of_pages; i++)
	{
		uint32 cur_va = va + i * PAGE_SIZE;

		int ret = alloc_page(ptr_page_directory, cur_va, PERM_WRITEABLE, 1);
		if (ret != 0)
		{

			for (uint32 j = 0; j < pages_allocated; j++)
			{
				uint32 rollback_va = va + j * PAGE_SIZE;
				unmap_frame(ptr_page_directory, rollback_va);
			}

			acquire_kspinlock(&kheap_lock);
			kheapPageAllocBreak = old_break;
			release_kspinlock(&kheap_lock);
			return NULL;
		}

		pages_allocated++;
	}

	acquire_kspinlock(&kheap_lock);

	for (uint32 i = 0; i < num_of_pages; i++)
	{
		uint32 cur_va = va + i * PAGE_SIZE;

		/* Use your existing helper kheap_physical_address -> to_frame_info pattern */
		struct FrameInfo *fi = to_frame_info(kheap_physical_address(cur_va));

		if (fi == NULL)
		{

			release_kspinlock(&kheap_lock);
			for (uint32 j = 0; j < pages_allocated; j++)
			{
				uint32 rollback_va = va + j * PAGE_SIZE;
				unmap_frame(ptr_page_directory, rollback_va);
			}

			acquire_kspinlock(&kheap_lock);
			kheapPageAllocBreak = old_break;
			release_kspinlock(&kheap_lock);

			return NULL;
		}

		fi->allocation_size = (i == 0) ? num_of_pages : 0;
		fi->is_start_of_alloc = (i == 0) ? 1 : 0;
	}

	release_kspinlock(&kheap_lock);

	return (void *)va;
}

	// TODO: [PROJECT'25.BONUS#3] FAST PAGE ALLOCATOR
}

//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
void kfree(void *virtual_address)
{
	//	If virtual address inside the [BLOCK ALLOCATOR] range
	// Use dynamic allocator to free the given address
	//	If virtual address inside the [PAGE ALLOCATOR] range
	// FREE the space of the given address from RAM
	//	Else (i.e. invalid address): should panic(�)

	acquire_kspinlock(&kheap_lock);

	uint32 va = (uint32)virtual_address; // cast since the passed parameter is a ptr
	uint32 *table_ptr = NULL;
	if (va >= dynAllocStart && va < dynAllocEnd)
	{
		release_kspinlock(&kheap_lock);
		free_block(virtual_address);
		return;
	}
	else if (va >= kheapPageAllocStart && va < kheapPageAllocBreak)
	{
		struct FrameInfo *frameptr = get_frame_info(ptr_page_directory, va, &table_ptr);
		if (frameptr != NULL)
		{
			if (!frameptr->is_start_of_alloc)
			{
				release_kspinlock(&kheap_lock);
				panic("Invalid Address; Not Start Of A Block!");
			}
			release_kspinlock(&kheap_lock);
			uint32 size = frameptr->allocation_size;
			for (int i = 0; i < size; i++)
			{
				uint32 current_va = va + (i * PAGE_SIZE);
				unmap_frame(ptr_page_directory, current_va);
			}

			uint32 end_of_freed_frames = va + (size * PAGE_SIZE);
			if (end_of_freed_frames == kheapPageAllocBreak)
			{
				while (kheapPageAllocBreak > kheapPageAllocStart)
				{
					uint32 last_page = kheapPageAllocBreak - PAGE_SIZE;
					struct FrameInfo *lastframe = get_frame_info(ptr_page_directory, last_page, &table_ptr);
					if (lastframe == NULL)
					{
						kheapPageAllocBreak -= PAGE_SIZE;
					}
					else
						break;
				}
			}
		}
		else
		{
			release_kspinlock(&kheap_lock);
			panic("Null Frame !");
		}
		release_kspinlock(&kheap_lock);
		return;
	}

	else
	{
		release_kspinlock(&kheap_lock);
		panic("Virtual Address Not Found!");
	}
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
	struct FrameInfo *frameptr = to_frame_info(physical_address);
	if (frameptr == NULL)
		return 0;
	uint32 offset = physical_address % PAGE_SIZE;
	return ((frameptr->va) + offset);
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
	bool within_blockalloc_range = (virtual_address >= dynAllocStart && virtual_address < dynAllocEnd);
	bool within_pagealloc_range = (virtual_address >= kheapPageAllocStart && virtual_address < kheapPageAllocBreak);

	if (!within_blockalloc_range && !within_pagealloc_range)
		return 0; // address outside kernel heap

	uint32 *table_ptr = NULL;
	struct FrameInfo *frameptr = get_frame_info(ptr_page_directory, virtual_address, &table_ptr);
	if (frameptr == NULL)
		return 0;
	else
	{
		uint32 page_table_index = PTX(virtual_address);
		uint32 page_table_entry = table_ptr[page_table_index];
		uint32 base_address = page_table_entry & (~0xFFF); // base address is the first 20 bits
		uint32 offset = virtual_address % PAGE_SIZE;

		return base_address + offset;
	}
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//
// krealloc():

//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to kmalloc().
//	A call with new_size = zero is equivalent to kfree().

extern __inline__ uint32 get_block_size(void *va);

void *krealloc(void *virtual_address, uint32 new_size)
{
	// TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - krealloc
	// Your code is here
	// Comment the following line
	panic("krealloc() is not implemented yet...!!");
}
