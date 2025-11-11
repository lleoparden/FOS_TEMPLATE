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
    	LIST_INSERT_TAIL(&freePagesList,&pageBlockInfoArr[i]);
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
	//we have 3 cases other than the memory full case
	//first case : there is a free block already in the freeblocklists
	//second case : there is a free page already in the freepageslist
	//third case : only free blocks are larger size

	uint32 blocksize = size;
	if(size< DYN_ALLOC_MIN_BLOCK_SIZE){
		blocksize = DYN_ALLOC_MIN_BLOCK_SIZE;
	}
	else{
		blocksize = round_up_to_power_of_2(blocksize);
	}

	int index = get_free_block_index(blocksize);

	struct BlockElement* tmpBlock;

	//first case
	if(!LIST_EMPTY(&freeBlockLists[index])){
		tmpBlock=LIST_FIRST(&freeBlockLists[index]);
		LIST_REMOVE(&freeBlockLists[index],tmpBlock);

		//update page
		uint32 page = ((uint32)tmpBlock - dynAllocStart)/PAGE_SIZE;
		struct PageInfoElement* pageInfo = &pageBlockInfoArr[page];
		pageInfo->num_of_free_blocks--;

		return tmpBlock;
	}
	//second case
	else if(!LIST_EMPTY(&freePagesList)){
		struct PageInfoElement* pageInfo = LIST_FIRST(&freePagesList);
		LIST_REMOVE(&freePagesList,pageInfo);

		pageInfo->block_size=blocksize;
		pageInfo->num_of_free_blocks=PAGE_SIZE/blocksize;

		if (get_page((void*)to_page_va(pageInfo)) < 0) {
        panic("alloc_block: get_page() failed");
    	}

		uint32 tmppoiter = to_page_va(pageInfo);
		//dividing pages into blocks and adding them to the freeblocklist
		for(int i =0; i< pageInfo->num_of_free_blocks;i++){
			LIST_INSERT_HEAD(&freeBlockLists[index],(struct BlockElement*)tmppoiter);
			tmppoiter+=blocksize;
		}

		if(!LIST_EMPTY(&freeBlockLists[index])){
			tmpBlock=LIST_FIRST(&freeBlockLists[index]);
			LIST_REMOVE(&freeBlockLists[index],tmpBlock);

			//update page
			uint32 page = ((uint32)tmpBlock - dynAllocStart)/PAGE_SIZE;
			struct PageInfoElement* pageInfo = &pageBlockInfoArr[page];
			pageInfo->num_of_free_blocks--;

			return tmpBlock;
		}
		
	}
	else{
		for(int i=index+1; i <= LOG2_MAX_SIZE-LOG2_MIN_SIZE;i++){
			if(!LIST_EMPTY(&freeBlockLists[i])){
				tmpBlock=LIST_FIRST(&freeBlockLists[i]);
				LIST_REMOVE(&freeBlockLists[i],tmpBlock);

				//update page
				uint32 page = ((uint32)tmpBlock - dynAllocStart)/PAGE_SIZE;
				struct PageInfoElement* pageInfo = &pageBlockInfoArr[page];
				pageInfo->num_of_free_blocks--;

				return tmpBlock;
			}
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

	uint32 blocksize = pageInfo->block_size;
	int index = get_free_block_index(blocksize);

	LIST_INSERT_HEAD(&freeBlockLists[index],(struct BlockElement*)va);
	pageInfo->num_of_free_blocks++;


	//incase all pages are now free
	if(pageInfo->num_of_free_blocks == PAGE_SIZE/blocksize){
        uint32 tmppoiter = to_page_va(pageInfo);
		//recollecting divided pages into blocks
		for(int i =0; i< PAGE_SIZE/blocksize; i++){
			LIST_REMOVE(&freeBlockLists[index],(struct BlockElement*)tmppoiter);
			tmppoiter+=blocksize;
		}
        return_page((void*)to_page_va(pageInfo));
        pageInfo->block_size = 0;
        pageInfo->num_of_free_blocks = 0;
        LIST_INSERT_HEAD(&freePagesList, pageInfo);
	}

	//Comment the following line
	// panic("free_block() Not implemented yet");
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
