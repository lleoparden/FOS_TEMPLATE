/*
 * fault_handler.c
 *
 *  Created on: Oct 12, 2022
 *      Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include <kern/mem/kheap.h>

//2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
// 0 means don't bypass the PAGE FAULT
uint8 bypassInstrLength = 0;

//===============================
// REPLACEMENT STRATEGIES
//===============================
//2020
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE ;
}
void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
/*2018*/ void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
/*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}
/*2024*/ void setFASTNchanceCLOCK(bool fast){ FASTNchanceCLOCK = fast; };
/*2025*/ void setPageReplacmentAlgorithmOPTIMAL(){ _PageRepAlgoType = PG_REP_OPTIMAL; };

//2020
uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
/*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmOPTIMAL(){if(_PageRepAlgoType == PG_REP_OPTIMAL) return 1; return 0;}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }

void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
uint8 isBufferingEnabled(){  return _EnableBuffering ; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}

//===============================
// FAULT HANDLERS
//===============================

//==================
// [0] INIT HANDLER:
//==================
void fault_handler_init()
{
	//setPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX);
	//setPageReplacmentAlgorithmOPTIMAL();
	setPageReplacmentAlgorithmCLOCK();
	//setPageReplacmentAlgorithmModifiedCLOCK();
	enableBuffering(0);
	enableModifiedBuffer(0) ;
	setModifiedBufferLength(1000);
}
//==================
// [1] MAIN HANDLER:
//==================
/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
int8 num_repeated_fault  = 0;
extern uint32 sys_calculate_free_frames() ;

struct Env* last_faulted_env = NULL;
void fault_handler(struct Trapframe *tf)
{
	/******************************************************/
	// Read processor's CR2 register to find the faulting address
	uint32 fault_va = rcr2();
	//cprintf("************Faulted VA = %x************\n", fault_va);
	//	print_trapframe(tf);
	/******************************************************/

	//If same fault va for 3 times, then panic
	//UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
	struct Env* cur_env = get_cpu_proc();
	if (last_fault_va == fault_va && last_faulted_env == cur_env)
	{
		num_repeated_fault++ ;
		if (num_repeated_fault == 3)
		{
			print_trapframe(tf);
			panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
		}
	}
	else
	{
		before_last_fault_va = last_fault_va;
		before_last_eip = last_eip;
		num_repeated_fault = 0;
	}
	last_eip = (uint32)tf->tf_eip;
	last_fault_va = fault_va ;
	last_faulted_env = cur_env;
	/******************************************************/
	//2017: Check stack overflow for Kernel
	int userTrap = 0;
	if ((tf->tf_cs & 3) == 3) {
		userTrap = 1;
	}
	if (!userTrap)
	{
		struct cpu* c = mycpu();
		//cprintf("trap from KERNEL\n");
		if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
			panic("User Kernel Stack: overflow exception!");
		else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
			panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
#if USE_KHEAP
		if (fault_va >= KERNEL_HEAP_MAX)
			panic("Kernel: heap overflow exception!");
#endif
	}
	//2017: Check stack underflow for User
	else
	{
		//cprintf("trap from USER\n");
		if (fault_va >= USTACKTOP && fault_va < USER_TOP)
			panic("User: stack underflow exception!");
	}

	//get a pointer to the environment that caused the fault at runtime
	//cprintf("curenv = %x\n", curenv);
	struct Env* faulted_env = cur_env;
	if (faulted_env == NULL)
	{
		cprintf("\nFaulted VA = %x\n", fault_va);
		print_trapframe(tf);
		panic("faulted env == NULL!");
	}
	//check the faulted address, is it a table or not ?
	//If the directory entry of the faulted address is NOT PRESENT then
	if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
	{
		faulted_env->tableFaultsCounter ++ ;
		table_fault_handler(faulted_env, fault_va);
	}
	else
	{
		if (userTrap)
		{
			/*============================================================================================*/
			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #2 Check for invalid pointers
			//(e.g. pointing to unmarked user heap page, kernel or wrong access rights),
			//your code is here

		uint32 permsPt = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);

			// cprintf("Present Bit:  %d\n",(permsPt & PERM_PRESENT));
			// cprintf("UHpage Bit:  %d\n",(permsPt & PERM_UHPAGE));
			// cprintf("User Bit:  %d\n",(permsPt & PERM_USER));
			// cprintf("Writable Bit:  %d\n",(permsPt & PERM_WRITEABLE));

				if (fault_va >= KERNEL_BASE) {
					//cprintf("\nIn kernel\n");
    				env_exit();
				}

				else if (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX) {
    				if ((permsPt & PERM_UHPAGE) == 0){
        				//cprintf("\nUnmarked user heap\n");
        				env_exit();
    				}
				}

				else if ((permsPt & PERM_PRESENT) && (!(permsPt & PERM_WRITEABLE) || !(permsPt & PERM_USER))) {
    				//cprintf("\nNot writable\n");
    				env_exit();
				}

			/*============================================================================================*/
		}

		/*2022: Check if fault due to Access Rights */
		int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
		if (perms & PERM_PRESENT)
			panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;
		/*============================================================================================*/


		// we have normal page fault =============================================================
		faulted_env->pageFaultsCounter ++ ;

//				cprintf("[%08s] user PAGE fault va %08x\n", faulted_env->prog_name, fault_va);
//				cprintf("\nPage working set BEFORE fault handler...\n");
//				env_page_ws_print(faulted_env);
		//int ffb = sys_calculate_free_frames();

		if(isBufferingEnabled())
		{
			__page_fault_handler_with_buffering(faulted_env, fault_va);
		}
		else
		{
			page_fault_handler(faulted_env, fault_va);
		}

		//		cprintf("\nPage working set AFTER fault handler...\n");
		//		env_page_ws_print(faulted_env);
		//		int ffa = sys_calculate_free_frames();
		//		cprintf("fault handling @%x: difference in free frames (after - before = %d)\n", fault_va, ffa - ffb);
	}

	/*************************************************************/
	//Refresh the TLB cache
	tlbflush();
	/*************************************************************/
}


//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env * curenv, uint32 fault_va)
{
	//panic("table_fault_handler() is not implemented yet...!!");
	//Check if it's a stack page
	uint32* ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//=========================
// [3] PAGE FAULT HANDLER:
//=========================
/* Calculate the number of page faults according th the OPTIMAL replacement strategy
 * Given:
 * 	1. Initial Working Set List (that the process started with)
 * 	2. Max Working Set Size
 * 	3. Page References List (contains the stream of referenced VAs till the process finished)
 *
 * 	IMPORTANT: This function SHOULD NOT change any of the given lists
 */
int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
{
	//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #2 get_optimal_num_faults
	//Your code is here
	//Comment the following line
	panic("get_optimal_num_faults() is not implemented yet...!!");
}

void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
{
#if USE_KHEAP
	if (isPageReplacmentAlgorithmOPTIMAL())
	{
		//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Reference Stream
		//Your code is here
		fault_va = ROUNDDOWN(fault_va, PAGE_SIZE);
    //-----------------------------------------
    // 1) Check if the faulted page is in the Active Working Set
    //-----------------------------------------
    bool inWS = 0;
    struct WorkingSetElement *wse;

    LIST_FOREACH(wse, &(faulted_env->page_WS_list))
    {
        if (wse->virtual_address == fault_va) {
            inWS = 1;
			cprintf("Page is already in WS\n");
            // mark as present again (it was forced to 0)
            pt_set_page_permissions(faulted_env->env_page_directory, fault_va, PERM_PRESENT, 0);
            break;
        }
    }

    //-----------------------------------------
    // 2) Page NOT in Active WS → we must insert it
    //-----------------------------------------
    if (!inWS)
    {
		cprintf("Page not in WS, inserting it\n");
        uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));

        // --------------------------------------------
        // If FULL → reset present bit for all WS pages & clear WS
        // --------------------------------------------
        if (wsSize == faulted_env->page_WS_max_size-1)
        {
			cprintf("WS is full, clearing it\n");
            struct WorkingSetElement *p, *next;
            p = LIST_FIRST(&(faulted_env->page_WS_list));

            while (p != NULL) {
                next = LIST_NEXT(p);
				cprintf("Clearing page %x from WS\n", p->virtual_address);
                // clear its PRESENT bit
                pt_set_page_permissions(faulted_env->env_page_directory, p->virtual_address, 0, PERM_PRESENT);

                // remove from WS but do NOT unmap frame
                LIST_REMOVE(&(faulted_env->page_WS_list), p);

                kfree(p);
                p = next;
            }
			cprintf("Cleared all pages from WS\n");
            faulted_env->page_last_WS_element = NULL;
        }

        // --------------------------------------------
        // If the page does not exist on PF → illegal
        // --------------------------------------------
		// cprintf("Before");
        // int ret = pf_read_env_page(faulted_env, (void*)fault_va);
		// cprintf("After");		
        // if (ret == E_PAGE_NOT_EXIST_IN_PF) {
		// 	cprintf("Page does not exist in PF, exiting env\n");
        //     if (!(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) && !(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX))
        //     {
        //         env_exit();
        //     }
        // }

        // --------------------------------------------
        // Insert new page into WS
        struct FrameInfo *frame;
			if (allocate_frame(&frame) != 0) {
        		panic("LRU: allocate_frame failed");
   			}

			cprintf("Skibidi Inserting page %x into WS\n", fault_va);

			fault_va = ROUNDDOWN(fault_va, PAGE_SIZE);

			struct WorkingSetElement *Element = env_page_ws_list_create_element(faulted_env, fault_va);

			map_frame(faulted_env->env_page_directory, frame, fault_va, PERM_PRESENT | PERM_WRITEABLE | PERM_USER);


			int faultPage = pf_read_env_page(faulted_env, (void*) fault_va);

			if (faultPage == E_PAGE_NOT_EXIST_IN_PF){
				if (!(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) && !(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)){
					unmap_frame(faulted_env->env_page_directory, fault_va);
					env_exit();
				}
			}

			if(faulted_env->page_last_WS_element == NULL ){
			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), Element);
			if(wsSize == faulted_env->page_WS_max_size-1){
				faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
			}
			}
			else
			LIST_INSERT_BEFORE(&(faulted_env->page_WS_list), (faulted_env -> page_last_WS_element), Element);\

			cprintf("Inserted page %x into WS\n", fault_va);

        // Mark present
    	uint32 *ptr_table;
		cprintf("sigma\n");
		get_frame_info(faulted_env->env_page_directory, fault_va, &ptr_table);
		cprintf("boy\n");
		ptr_table[PTX(fault_va)] &= PERM_PRESENT;
    }

    //-----------------------------------------
    // 3) Append to the REFERENCE STREAM
    //-----------------------------------------

    struct PageRefElement *ref =
        (struct PageRefElement*) kmalloc(sizeof(struct PageRefElement));
    if (ref == NULL){
        panic("kmalloc() failed when creating PageRefElement");
	}
    ref->virtual_address = fault_va;
	LIST_INSERT_TAIL(&(faulted_env->referenceStreamList), ref);
	cprintf("Appended page %x to reference stream\n", fault_va);
		//Comment the following line
		//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
	}
	else
	{
		struct WorkingSetElement *victimWSElement = NULL;
		uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
		if(wsSize < (faulted_env->page_WS_max_size))
		{
			//TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #3 placement
			//Your code is here
			struct FrameInfo *frame;
			if (allocate_frame(&frame) != 0) {
        		panic("LRU: allocate_frame failed");
   			}

			fault_va = ROUNDDOWN(fault_va, PAGE_SIZE);

			struct WorkingSetElement *Element = env_page_ws_list_create_element(faulted_env, fault_va);

			map_frame(faulted_env->env_page_directory, frame, fault_va, PERM_PRESENT | PERM_WRITEABLE | PERM_USER);


			int faultPage = pf_read_env_page(faulted_env, (void*) fault_va);

			if (faultPage == E_PAGE_NOT_EXIST_IN_PF){
				if (!(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) && !(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)){
					unmap_frame(faulted_env->env_page_directory, fault_va);
					env_exit();
				}
			}

			if(faulted_env->page_last_WS_element == NULL ){
			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), Element);
			if(wsSize == faulted_env->page_WS_max_size-1){
				faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
			}
			}
			else
			LIST_INSERT_BEFORE(&(faulted_env->page_WS_list), (faulted_env -> page_last_WS_element), Element);
			//Comment the following line
			// panic("page_fault_handler().PLACEMENT is not implemented yet...!!");
		}
		else
		{
			if (isPageReplacmentAlgorithmCLOCK())
			{
				//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
				//Your code is here
				//Comment the following line
				//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
				//Your code is here
				unsigned int oldest_stamp = 0xFFFFFFFF;
				struct WorkingSetElement *wse;
				LIST_FOREACH(wse, &(faulted_env->page_WS_list)) {
					if (wse->time_stamp < oldest_stamp) {
						oldest_stamp = wse->time_stamp;
						victimWSElement = wse;
					}
				}

				if (victimWSElement == NULL)
				panic("LRU: no victim selected!");

				uint32 permsPt = pt_get_page_permissions(faulted_env->env_page_directory, victimWSElement->virtual_address);
				uint32 *ptr_table;
				struct FrameInfo *frame_info = get_frame_info(faulted_env->env_page_directory,victimWSElement->virtual_address,&ptr_table);
				if(frame_info == NULL)
				{
					panic("page_fault_handler: frame_info is NULL for the victim page to be replaced!");
				}
				if(permsPt & PERM_MODIFIED) {
					pf_update_env_page(faulted_env, victimWSElement->virtual_address, frame_info);
				}
				unmap_frame(faulted_env->env_page_directory, victimWSElement->virtual_address);


				//Remove the victim from the WS list
				LIST_REMOVE(&(faulted_env->page_WS_list),victimWSElement);

				if (faulted_env->page_last_WS_element == victimWSElement) {
					faulted_env->page_last_WS_element =LIST_NEXT(victimWSElement);

					if (faulted_env->page_last_WS_element == NULL){
						faulted_env->page_last_WS_element =LIST_FIRST(&(faulted_env->page_WS_list));
					}
				}
				kfree(victimWSElement);



				//Now, we can place the new page
				struct FrameInfo *frame;
			if (allocate_frame(&frame) != 0) {
        		panic("LRU: allocate_frame failed");
   			}

			fault_va = ROUNDDOWN(fault_va, PAGE_SIZE);

			struct WorkingSetElement *Element = env_page_ws_list_create_element(faulted_env, fault_va);

			map_frame(faulted_env->env_page_directory, frame, fault_va, PERM_PRESENT | PERM_WRITEABLE | PERM_USER);


			int faultPage = pf_read_env_page(faulted_env, (void*) fault_va);

			if (faultPage == E_PAGE_NOT_EXIST_IN_PF){
				if (!(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) && !(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)){
					unmap_frame(faulted_env->env_page_directory, fault_va);
					env_exit();
				}
			}

			if(faulted_env->page_last_WS_element == NULL ){
			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), Element);
			if(wsSize == faulted_env->page_WS_max_size-1){
				faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
			}
			}
			else
			LIST_INSERT_BEFORE(&(faulted_env->page_WS_list), (faulted_env -> page_last_WS_element), Element);




				//Comment the following line
				// panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
			else if (isPageReplacmentAlgorithmModifiedCLOCK())
			{
				//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
				//Your code is here

				struct WorkingSetElement *wse;


				struct WorkingSetElement *start =faulted_env->page_last_WS_element;

				
				while(victimWSElement == NULL)
				{
					//first trial 	
					wse = start;
					do {
						wse = LIST_NEXT(wse);
						if (wse == NULL)
							wse = LIST_FIRST(&(faulted_env->page_WS_list));
						uint32 permsPt = pt_get_page_permissions(faulted_env->env_page_directory, wse->virtual_address);
						if (!(permsPt & PERM_USED) && !(permsPt & PERM_MODIFIED)) {
							victimWSElement = wse;
							break;
						}
					}while(wse != start);
					//second trial
					if (victimWSElement == NULL ) {
						wse = start;
						do {
							wse = LIST_NEXT(wse);
							if (wse == NULL)
								wse = LIST_FIRST(&(faulted_env->page_WS_list));
							uint32 permsPt = pt_get_page_permissions(faulted_env->env_page_directory, wse->virtual_address);
							if (!(permsPt & PERM_USED) ) {
								victimWSElement = wse;
								break;
							}else{
								//clear used bit
								uint32 *ptr_table;
								get_frame_info(faulted_env->env_page_directory, wse->virtual_address, &ptr_table);
								ptr_table[PTX(wse->virtual_address)] &= ~PERM_USED;
							}
						}while(wse  != start);
					}
				}
				faulted_env->page_last_WS_element = LIST_NEXT(victimWSElement);

				if (faulted_env->page_last_WS_element == NULL){
					faulted_env->page_last_WS_element =LIST_FIRST(&(faulted_env->page_WS_list));
				}

				unmap_frame(faulted_env->env_page_directory, victimWSElement->virtual_address);


				//Remove the victim from the WS list
				LIST_REMOVE(&(faulted_env->page_WS_list),victimWSElement);

				if (faulted_env->page_last_WS_element == victimWSElement) {
					faulted_env->page_last_WS_element =LIST_NEXT(victimWSElement);

					if (faulted_env->page_last_WS_element == NULL){
						faulted_env->page_last_WS_element =LIST_FIRST(&(faulted_env->page_WS_list));
					}
				}
				kfree(victimWSElement);



				//Now, we can place the new page
				struct FrameInfo *frame;
			if (allocate_frame(&frame) != 0) {
        		panic("LRU: allocate_frame failed");
   			}

			fault_va = ROUNDDOWN(fault_va, PAGE_SIZE);

			struct WorkingSetElement *Element = env_page_ws_list_create_element(faulted_env, fault_va);

			map_frame(faulted_env->env_page_directory, frame, fault_va, PERM_PRESENT | PERM_WRITEABLE | PERM_USER);


			int faultPage = pf_read_env_page(faulted_env, (void*) fault_va);

			if (faultPage == E_PAGE_NOT_EXIST_IN_PF){
				if (!(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) && !(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)){
					unmap_frame(faulted_env->env_page_directory, fault_va);
					env_exit();
				}
			}

			if(faulted_env->page_last_WS_element == NULL ){
			LIST_INSERT_TAIL(&(faulted_env->page_WS_list), Element);
			if(wsSize == faulted_env->page_WS_max_size-1){
				faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
			}
			}
			else
			LIST_INSERT_BEFORE(&(faulted_env->page_WS_list), (faulted_env -> page_last_WS_element), Element);



				//Comment the following line
				//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
			}
		}
	}
#endif
}


void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}



