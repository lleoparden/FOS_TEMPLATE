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
#if USE_KHEAP

	uint32 *ptr_page_table = NULL;
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
		int gt_ret = TABLE_NOT_EXIST;
		struct FrameInfo *info = NULL;

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

					struct FrameInfo *fi = get_frame_info(ptr_page_directory, fara8_start + i * PAGE_SIZE, &ptr_page_table);
					if (fi == NULL)
						panic("kmalloc: NULL FRAME in exact-fit");

					if (i == 0){
					    fi->allocation_size = num_of_pages;}
					else{
					    fi->allocation_size = 0;
					}

					if (i == 0) {
					    fi->is_start_of_alloc = 1;
					}
					else {
					    fi->is_start_of_alloc = 0;
					}

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
			struct FrameInfo *fi = get_frame_info(ptr_page_directory, worstfit_start + (i * PAGE_SIZE), &ptr_page_table);
			if (fi == NULL)
				panic("kmalloc: NULL FRAME in worst-fit");
			if (i == 0){
					    fi->allocation_size = num_of_pages;}
					else{
					    fi->allocation_size = 0;
					}

					if (i == 0) {
					    fi->is_start_of_alloc = 1;
					}
					else {
					    fi->is_start_of_alloc = 0;
					}
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

				if (i == 0){
					    fi->allocation_size = num_of_pages;}
					else{
					    fi->allocation_size = 0;
					}

					if (i == 0) {
					    fi->is_start_of_alloc = 1;
					}
					else {
					    fi->is_start_of_alloc = 0;
					}
	}

	release_kspinlock(&kheap_lock);

	return (void *)va;

}

#else
	panic("kmalloc: USE_KHEAP not enabled!");
#endif



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
	//	Else (i.e. invalid address): should panic
#if USE_KHEAP

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
			if ((frameptr->is_start_of_alloc)==0)
			{
				release_kspinlock(&kheap_lock);
				panic("Invalid Address: not a start of a block!");
			}

			uint32 size = frameptr->allocation_size;
			for (int i = 0; i < size; i++)
			{
				uint32 current_va = va + (i * PAGE_SIZE);
				unmap_frame(ptr_page_directory, current_va);
			}

			uint32 final_frame = va + (size * PAGE_SIZE);
			if (final_frame == kheapPageAllocBreak)
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
			release_kspinlock(&kheap_lock);
			return;
		}
		else
		{
			release_kspinlock(&kheap_lock);
			panic("Null frame.");
		}
	}

	else
	{
		release_kspinlock(&kheap_lock);
		panic("Virtual Address not found!");
	}
#else
	panic("kfree: USE_KHEAP not enabled!");
#endif
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
#if USE_KHEAP
	uint32 pa = (uint32)physical_address;
	struct FrameInfo *frameptr = to_frame_info(pa);
	if (frameptr == NULL)
		return 0;
	if (frameptr->va == 0)
		return 0; // to avoid returning an address with non zero offset
	uint32 offset = pa % PAGE_SIZE;
	return ((frameptr->va) + offset);
#else
	panic("kheap_virtual_address: USE_KHEAP not enabled!");
#endif
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
#if USE_KHEAP
	uint32 va = (uint32)virtual_address;
	bool blockalloc_range = (va >= dynAllocStart && va < dynAllocEnd);
	bool pagealloc_range = (va >= kheapPageAllocStart && va < kheapPageAllocBreak);

	if (!blockalloc_range && !pagealloc_range)
		return 0; // address outside kernel heap

	uint32 *table_ptr = NULL;
	struct FrameInfo *frameptr = get_frame_info(ptr_page_directory, va, &table_ptr);
	if (frameptr == NULL)
		return 0;
	else
	{
		uint32 pt_indx = PTX(va);
		uint32 pt_entry = table_ptr[pt_indx];
		uint32 base_addr = pt_entry & (~0xFFF); // base address is the first 20 bits
		uint32 offset = va % PAGE_SIZE;

		return base_addr + offset;
	}
#else
	panic("kheap_physical_address: USE_KHEAP not enabled!");
#endif
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
	// panic("krealloc() is not implemented yet...!!");
#if USE_KHEAP

	if (virtual_address == NULL)
	{
		return kmalloc(new_size);
	}

	if (new_size == 0)
	{
		kfree(virtual_address);
		return NULL;
	}

	uint32 va = (uint32)virtual_address;

	acquire_kspinlock(&kheap_lock);

	if (va >= dynAllocStart && va < dynAllocEnd)
	{
		release_kspinlock(&kheap_lock);
		void *new_va = realloc_block(virtual_address, new_size);
		return new_va;
	}

	if (va >= kheapPageAllocStart && va < kheapPageAllocBreak)
	{
		uint32 *table = NULL;
		struct FrameInfo *fi = get_frame_info(ptr_page_directory, va, &table);

		if (fi == NULL || fi->is_start_of_alloc == 0)
		{
			release_kspinlock(&kheap_lock);
			panic("krealloc: invalid pointer (not start of page block)");
		}

		uint32 pages = fi->allocation_size;
		uint32 size = pages * PAGE_SIZE;

		uint32 old_pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
		uint32 new_pages = ROUNDUP(new_size, PAGE_SIZE) / PAGE_SIZE;

		if (old_pages == new_pages)
		{
			release_kspinlock(&kheap_lock);
			return virtual_address;
		}

		release_kspinlock(&kheap_lock);
		void *new_ptr = kmalloc(new_size);
		acquire_kspinlock(&kheap_lock);
		if (!new_ptr)
		{
			release_kspinlock(&kheap_lock);
			return NULL;
		}

		memcpy(new_ptr, virtual_address, MIN(new_size, size));
		release_kspinlock(&kheap_lock);
		kfree(virtual_address);
		return new_ptr;
	}
	release_kspinlock(&kheap_lock);
	panic("krealloc: invalid pointer");
	return NULL;

#else
	panic("krealloc: USE_KHEAP not enabled!");
#endif
}
