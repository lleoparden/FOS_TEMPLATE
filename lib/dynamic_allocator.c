/*
 * dynamic_allocator*
 *  Created on: Sep 21, 2023
 *      Author: HP
 */
#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==================================
// [1] GET PAGE VA:
//==================================
__inline__ uint32 to_page_va(struct PageInfoElement *ptrPageInfo)
{
	//Get start VA of the page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #1 initialize_dynamic_allocator
	//Your code is here

	dynAllocStart=daStart;
    dynAllocEnd=daEnd;

	LIST_INIT(&freePagesList);

	for(int i =0; i <= LOG2_MAX_SIZE - LOG2_MIN_SIZE ; i++){
	LIST_INIT(&freeBlockLists[i]);
	}


	for(int i =0; i < ((daEnd-daStart)/PAGE_SIZE); i++){
	 pageBlockInfoArr[i].block_size=0;
     pageBlockInfoArr[i].num_of_free_blocks=0;
    LIST_INSERT_HEAD(&freePagesList,&pageBlockInfoArr[i]);
	}

}

//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size
	//Your code is here

	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}

	return pageBlockInfoArr[((uint32)va-dynAllocStart)/PAGE_SIZE].block_size;


}

//===========================
// 3) ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 size)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
		assert(size != 0);
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block
	//Your code is here
	uint32 workingSize = size;
	if(size< DYN_ALLOC_MIN_BLOCK_SIZE){
		workingSize = DYN_ALLOC_MIN_BLOCK_SIZE;
	}
	else{
		workingSize = round_up_to_power_of_2(workingSize);
	}

	int index = get_free_block_index(workingSize);

	//first case: free block exists
	if(LIST_FIRST(&freeBlockLists[index])!=NULL){
		struct BlockElement *blk = LIST_FIRST(&freeBlockLists[index]);
		LIST_REMOVE(&freeBlockLists[index],blk);

		//update page info
		uint32 page = ((uint32)blk - dynAllocStart)/PAGE_SIZE;
		pageBlockInfoArr[page].num_of_free_blocks--;

		return (void*)blk;
	}

	//second case: free page exists
	else if(LIST_FIRST(&freePagesList)!=NULL){
		struct PageInfoElement* workingPage = LIST_FIRST(&freePagesList);
		LIST_REMOVE(&freePagesList,workingPage);

		void* pageVA = (void*)to_page_va(workingPage);

		if (get_page(pageVA) < 0) {
        panic("alloc_block: get_page() failed");
    	}

		workingPage->block_size = workingSize;
		workingPage->num_of_free_blocks = PAGE_SIZE/workingSize;

		uint8* pointer = pageVA;
		for(uint32 i = 0; i< workingPage->num_of_free_blocks; i++){
			struct BlockElement* current = (struct BlockElement*)(pointer + i * workingSize);
			LIST_INSERT_HEAD(&freeBlockLists[index],current);
		}

		struct BlockElement *blk = LIST_FIRST(&freeBlockLists[index]);
		LIST_REMOVE(&freeBlockLists[index],blk);

		//update page info
		uint32 page = ((uint32)blk - dynAllocStart)/PAGE_SIZE;
		pageBlockInfoArr[page].num_of_free_blocks--;

		return (void*)blk;
	}

	//third case: no free page or block
	for(int i = index +1; i<= (LOG2_MAX_SIZE-LOG2_MIN_SIZE);i++)
	{
		if(LIST_FIRST(&freeBlockLists[i])!=NULL){
			struct BlockElement *blk = LIST_FIRST(&freeBlockLists[i]);
			LIST_REMOVE(&freeBlockLists[i],blk);

			//update page info
			uint32 page = ((uint32)blk - dynAllocStart)/PAGE_SIZE;
			pageBlockInfoArr[page].num_of_free_blocks--;

			return (void*)blk;
		}
	}

	//fourth case: no free block
	panic("Memory full");



	//Comment the following line
	//panic("alloc_block() Not implemented yet");

	//TODO: [PROJECT'25.BONUS#1] DYNAMIC ALLOCATOR - block if no free block
}

//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================

	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #4 free_block
	//Your code is here

	uint32 page = ((uint32)va - dynAllocStart)/PAGE_SIZE;
	struct PageInfoElement* pageInfo = &pageBlockInfoArr[page];

	uint32 blockSize = pageInfo->block_size;
	int index = get_free_block_index(blockSize);

	LIST_INSERT_HEAD(&freeBlockLists[index],(struct BlockElement*)va);
	pageInfo->num_of_free_blocks++;


	//if all blocks are free, return page
	if(pageInfo->num_of_free_blocks == PAGE_SIZE/blockSize){
		LIST_REMOVE(&freePagesList,pageInfo);
		void* page = (void*)to_page_va(pageInfo);
		return_page(page);
		pageInfo->block_size = 0;
		pageInfo->num_of_free_blocks = 0;
		LIST_INSERT_HEAD(&freePagesList,pageInfo);
	}

	//Comment the following line
	panic("free_block() Not implemented yet");
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - realloc_block
	//Your code is here
	//Comment the following line
	panic("realloc_block() Not implemented yet");
}



//===========================
// Helper functions:
//===========================

int get_free_block_index(uint32 size){
	int index = 0;
	uint32 x = size;
	while ( x > DYN_ALLOC_MIN_BLOCK_SIZE ) {
		x = x >> 1;
		index++;
	}
	return index;
}

uint32 round_up_to_power_of_2(uint32 x){
	uint32 tmp = 1;
    while (tmp < x)
        tmp <<= 1;
    return tmp;
}
