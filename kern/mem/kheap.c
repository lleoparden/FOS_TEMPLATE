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
void kheap_init()
{
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

	size = ROUNDUP(size, PAGE_SIZE);

	if (size == 0)
        return NULL;

    if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
        return alloc_block(size);

    size = ROUNDUP(size, PAGE_SIZE);

    uint32 worstfit_size = 0;
    uint32 worstfit_start = 0;
    uint32 ptr = kheapPageAllocStart;

    while (ptr < kheapPageAllocBreak)
    {
        struct FrameInfo *info = get_frame_info(ptr_page_directory, ptr, NULL);

        if (info != NULL)
        {
            ptr += info->allocation_size * PAGE_SIZE;
        }
        else
        {
            uint32 fara8_start = ptr;
            uint32 fara8_size = 0;

            while (ptr < kheapPageAllocBreak)
            {
                info = get_frame_info(ptr_page_directory, ptr, NULL);
                if (info != NULL)
                    break;
                ptr += PAGE_SIZE;
                fara8_size += PAGE_SIZE;
            }


            if (fara8_size == size)
            {
                for (uint32 i = 0; i < size / PAGE_SIZE; i++)
                    alloc_page(ptr_page_directory, fara8_start + (i * PAGE_SIZE), PERM_WRITEABLE, 1);

                struct FrameInfo *fi = get_frame_info(ptr_page_directory, fara8_start, NULL);
                fi->allocation_size = size / PAGE_SIZE;
                return (void*)fara8_start;
            }

            else if (fara8_size > worstfit_size)
            {
                worstfit_size  = fara8_size;
                worstfit_start = fara8_start;
            }
        }
    }


    if (worstfit_size >= size)
    {
        for (uint32 i = 0; i < size / PAGE_SIZE; i++)
            alloc_page(ptr_page_directory, worstfit_start + (i * PAGE_SIZE), PERM_WRITEABLE, 1);

        struct FrameInfo *fi = get_frame_info(ptr_page_directory, worstfit_start, NULL);
        fi->allocation_size = size / PAGE_SIZE;
        return (void*)worstfit_start;
    }

    return NULL; 


	// TODO: [PROJECT'25.BONUS#3] FAST PAGE ALLOCATOR
}



//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
void kfree(void *virtual_address)
{

	//	 If virtual address inside the [BLOCK ALLOCATOR] range
	// Use dynamic allocator to free the given address
	//	If virtual address inside the [PAGE ALLOCATOR] range
	// FREE the space of the given address from RAM
	//	Else (i.e. invalid address): should panic(�)

	uint32 va = (uint32)virtual_address;
	uint32 *ptr_table = NULL;
	if (va >= dynAllocStart && va < dynAllocEnd)
		free_block(virtual_address);

	else if (va >= kheapPageAllocStart && va < kheapPageAllocBreak)
	{
		struct FrameInfo *fi = get_frame_info(ptr_page_directory, va, &ptr_table);
		if (fi != NULL)
		{
			uint32 size = fi->allocation_size;
			for (int i = 0; i < size; i++)
			{
				uint32 current_vadd = va + (i * PAGE_SIZE);
				unmap_frame(ptr_page_directory, current_vadd);
			}

			uint32 end_of_freed_frames = va + (size * PAGE_SIZE);
			if (end_of_freed_frames == kheapPageAllocBreak)
			{
				while (kheapPageAllocBreak > kheapPageAllocStart)
				{
					uint32 current_page = kheapPageAllocBreak - PAGE_SIZE;
					struct FrameInfo *currentfi = get_frame_info(ptr_page_directory, current_page, &ptr_table);
					if (currentfi == NULL)
					{
						kheapPageAllocBreak -= PAGE_SIZE;
					}
					else
						break;
				}
			}
		}
		else
			panic("Null Frame Info!");
	}

	else
		panic("Virtual Address Not Found!");
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
	struct FrameInfo *fi = to_frame_info(physical_address);
	if (fi == NULL)
		return 0;
	uint32 offset = physical_address % PAGE_SIZE;
	return fi->va + offset;
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
	// TODO: [PROJECT'25.GM#2] KERNEL HEAP - #4 kheap_physical_address
	// Your code is here
	// Comment the following line
	panic("kheap_physical_address() is not implemented yet...!!");

	/*EFFICIENT IMPLEMENTATION ~O(1) IS REQUIRED */
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
