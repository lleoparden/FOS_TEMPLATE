#include <inc/lib.h>
#define max_uheap_pages ((USER_HEAP_MAX - USER_HEAP_START) / PAGE_SIZE)
uint32 UHeapArr[max_uheap_pages];
//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE USER HEAP:
//==============================================
int __firstTimeFlag = 1;
void uheap_init()
{
	if (__firstTimeFlag)
	{
		init_uspinlock(&uheaplock, "user heap", 1);
		initialize_dynamic_allocator(USER_HEAP_START, USER_HEAP_START + DYN_ALLOC_MAX_SIZE);
		uheapPlaceStrategy = sys_get_uheap_strategy();
		uheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		uheapPageAllocBreak = uheapPageAllocStart;

		__firstTimeFlag = 0;
	}
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void *va)
{
	int ret = __sys_allocate_page(ROUNDDOWN(va, PAGE_SIZE), PERM_USER | PERM_WRITEABLE | PERM_UHPAGE);
	if (ret < 0)
		panic("get_page() in user: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void *va)
{
	int ret = __sys_unmap_frame(ROUNDDOWN((uint32)va, PAGE_SIZE));
	if (ret < 0)
		panic("return_page() in user: failed to return a page to the kernel");
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//=================================
// [1] ALLOCATE SPACE IN USER HEAP:
//=================================
void *malloc(uint32 size)
{
	//==============================================================
	// DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0)
		return NULL;
	//==============================================================
	// TODO: [PROJECT'25.IM#2] USER HEAP - #1 malloc
	// Your code is here
	// Comment the following line
	// panic("malloc() is not implemented yet...!!");

	int allocindx = -1;
	uint32 actualsize = size;
	size = ROUNDUP(size, PAGE_SIZE);
	uint32 needed_pages = size / PAGE_SIZE;

	if (actualsize <= DYN_ALLOC_MAX_BLOCK_SIZE) // Block Allocator
		return alloc_block(actualsize);

	else // Page Allocator : Custom Fit
	{
		uint32 maxfreepage = 0;
		uint32 lim = ((uheapPageAllocBreak - USER_HEAP_START) / PAGE_SIZE);
		uint32 i = ((uheapPageAllocStart - USER_HEAP_START) / PAGE_SIZE);

		while (i < lim)
		{

			if (UHeapArr[i] != 0) // page allocated so skip whole page
				i += UHeapArr[i];

			else // page not allocated (free page)
			{
				uint32 freesize = 0;
				uint32 freestart = i;

				while (i < lim && UHeapArr[i] == 0) // see how many pages are free
				{
					freesize++;
					i++;
				}

				if (freesize == needed_pages)
				{
					allocindx = freestart;
					break;
				}

				if (freesize > needed_pages)
				{
					if (freesize > maxfreepage)
					{
						maxfreepage = freesize;
						allocindx = freestart;
					}
				}
			}
		}

		if (allocindx == -1) // extend break
		{
			if (uheapPageAllocBreak + size > USER_HEAP_MAX)
				return NULL; // no space in unused
			else
			{
				allocindx = ((uheapPageAllocBreak - USER_HEAP_START) / PAGE_SIZE);
				uheapPageAllocBreak += size;
			}
		}
	}

	uint32 alloc_addr = USER_HEAP_START + (allocindx * PAGE_SIZE);
	UHeapArr[allocindx] = needed_pages;
	sys_allocate_user_mem(alloc_addr, size);
	return (void *)alloc_addr;
}

//=================================
// [2] FREE SPACE FROM USER HEAP:
//=================================
void free(void *virtual_address)
{
	// TODO: [PROJECT'25.IM#2] USER HEAP - #3 free
	uint32 va = (uint32)virtual_address;
	if (va >= dynAllocStart && va < dynAllocEnd)
		{
			free_block(virtual_address);
			return;
		}
	else if (va >= uheapPageAllocStart && va < uheapPageAllocBreak)
	{
		uint32 indx = ((va-USER_HEAP_START)/PAGE_SIZE);
		uint32 reqpages = UHeapArr[indx];

		if (reqpages==0) panic("User asking to free an unallocated block!");
		else
		{
			UHeapArr[indx]=0;
		}

		sys_free_user_mem(va,reqpages*PAGE_SIZE);

		uint32 end_of_freed_pages = va + (reqpages * PAGE_SIZE);
		if (end_of_freed_pages == uheapPageAllocBreak)
		{
			uint32 arrstart= ((uheapPageAllocStart - USER_HEAP_START)/PAGE_SIZE);
			uint32 lim =((va - USER_HEAP_START)/PAGE_SIZE); //limit is the page we just freed
			uint32 maxusedindx=arrstart;

			for(uint32 i=arrstart;i<lim;)
			{
				if (UHeapArr[i] !=0) //found an allocated block
				{
					maxusedindx= i+UHeapArr[i];
					i+=UHeapArr[i];
				}

				else //empty block
				{
					i++;
				}
			}

			uheapPageAllocBreak = USER_HEAP_START +(maxusedindx *PAGE_SIZE);
		}
	}
	else
		panic("Invalid address outside user heap range!");
}

//=================================
// [3] ALLOCATE SHARED VARIABLE:
//=================================
void *smalloc(char *sharedVarName, uint32 size, uint8 isWritable)
{
	//==============================================================
	// DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0)
		return NULL;
	//==============================================================

	// TODO: [PROJECT'25.IM#3] SHARED MEMORY - #2 smalloc
	// Your code is here
	// Comment the following line
	//  panic("smalloc() is not implemented yet...!!");

	int allocindx = -1;
	size = ROUNDUP(size, PAGE_SIZE);
	uint32 needed_pages = size / PAGE_SIZE;
	uint32 maxfreepage = 0;
	uint32 lim = ((uheapPageAllocBreak - USER_HEAP_START) / PAGE_SIZE);
	uint32 i = ((uheapPageAllocStart - USER_HEAP_START) / PAGE_SIZE);

	acquire_uspinlock(&uheaplock);
	while (i < lim)
	{

		if (UHeapArr[i] != 0) // page allocated so skip whole page
			i += UHeapArr[i];

		else // page not allocated (free page)
		{
			uint32 freesize = 0;
			uint32 freestart = i;

			while (i < lim && UHeapArr[i] == 0) // see how many pages are free
			{
				freesize++;
				i++;
			}

			if (freesize == needed_pages)
			{
				allocindx = freestart;
				break;
			}

			if (freesize > needed_pages)
			{
				if (freesize > maxfreepage)
				{
					maxfreepage = freesize;
					allocindx = freestart;
				}
			}
		}
	}

	if (allocindx == -1) // extend break
	{
		if (uheapPageAllocBreak + size > USER_HEAP_MAX)
		{
			release_uspinlock(&uheaplock);
			return NULL; // no space in unused
		}
		else
		{
			allocindx = ((uheapPageAllocBreak - USER_HEAP_START) / PAGE_SIZE);
			uheapPageAllocBreak += size;
		}
	}

	uint32 allocVA = USER_HEAP_START + allocindx * PAGE_SIZE;
	int id = sys_create_shared_object(sharedVarName, size, isWritable, (void *)allocVA);
	if (id < 0)
	{
		release_uspinlock(&uheaplock);
		return NULL;
	}
	UHeapArr[allocindx] = needed_pages;
	release_uspinlock(&uheaplock);
	return (void *)allocVA;
}

//========================================
// [4] SHARE ON ALLOCATED SHARED VARIABLE:
//========================================
void *sget(int32 ownerEnvID, char *sharedVarName)
{
	//==============================================================
	// DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================

	// TODO: [PROJECT'25.IM#3] SHARED MEMORY - #4 sget
	// Your code is here
	// Comment the following line
	// panic("sget() is not implemented yet...!!");

	uint32 size = sys_size_of_shared_object(ownerEnvID, sharedVarName);
	if (size == E_SHARED_MEM_NOT_EXISTS)
		return NULL;

	void *allocVA = smalloc(sharedVarName, size, 1);
	if (allocVA == NULL)
		return NULL;

	int id = sys_get_shared_object(ownerEnvID, sharedVarName, (void *)allocVA);
	if (id < 0)
	{
		return NULL;
	}
	return allocVA;
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//=================================
// REALLOC USER SPACE:
//=================================
//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to malloc().
//	A call with new_size = zero is equivalent to free().

//  Hint: you may need to use the sys_move_user_mem(...)
//		which switches to the kernel mode, calls move_user_mem(...)
//		in "kern/mem/chunk_operations.c", then switch back to the user mode here
//	the move_user_mem() function is empty, make sure to implement it.
void *realloc(void *virtual_address, uint32 new_size)
{
	//==============================================================
	// DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================
	panic("realloc() is not implemented yet...!!");
}

//=================================
// FREE SHARED VARIABLE:
//=================================
//	This function frees the shared variable at the given virtual_address
//	To do this, we need to switch to the kernel, free the pages AND "EMPTY" PAGE TABLES
//	from main memory then switch back to the user again.
//
//	use sys_delete_shared_object(...); which switches to the kernel mode,
//	calls delete_shared_object(...) in "shared_memory_manager.c", then switch back to the user mode here
//	the delete_shared_object() function is empty, make sure to implement it.
void sfree(void *virtual_address)
{
	// TODO: [PROJECT'25.BONUS#5] EXIT #2 - sfree
	// Your code is here
	// Comment the following line
	panic("sfree() is not implemented yet...!!");

	//	1) you should find the ID of the shared variable at the given address
	//	2) you need to call sys_freeSharedObject()
}

//==================================================================================//
//========================== MODIFICATION FUNCTIONS ================================//
//==================================================================================//
