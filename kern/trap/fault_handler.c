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

	int numOfFaults = 0;
	struct WS_List wsList;
	LIST_INIT(&wsList);
	struct PageRefElement *refEl;

	struct WorkingSetElement *wse;
	LIST_FOREACH(wse, initWorkingSet) {
		struct WorkingSetElement *newWSE = (struct WorkingSetElement*)kmalloc(sizeof(struct WorkingSetElement));
		if (newWSE == NULL) {
			panic("kmalloc() failed when creating WorkingSetElement");
		}
		newWSE->virtual_address = wse->virtual_address;
		newWSE->empty = wse->empty;
		newWSE->time_stamp = wse->time_stamp;
		newWSE->sweeps_counter = wse->sweeps_counter;
		LIST_INSERT_TAIL(&wsList, newWSE);
	}
	LIST_FOREACH(refEl, pageReferences){

		// [3] Check if faulted page already in Active WS
        bool found = 0;
        LIST_FOREACH(wse, &wsList) {
            if (wse->virtual_address == refEl->virtual_address) {
                found = 1;
                break;
            }
        }
        
        // [3] If page is already in Active WS, do nothing (shouldn't happen if frame is NULL)
        if (found) {
            cprintf("3 for VA %x - Already in WS (unexpected!)\n", refEl->virtual_address);
        } else {
            // Page NOT in Active WS
            uint32 wsSize = LIST_SIZE(&wsList);
            numOfFaults++;

            // If Active WS is FULL, reset present & delete all its pages
            if (wsSize >= maxWSSize) {
                cprintf("3 for VA %x - WS FULL, clearing all pages\n", refEl->virtual_address);
                while (!LIST_EMPTY(&wsList)) {
    			struct WorkingSetElement *victimWSElement = LIST_FIRST(&wsList);
    			LIST_REMOVE(&wsList, victimWSElement);
				}
            }
            // [4] Add the faulted page to the Active WS
           struct WorkingSetElement *newWSE = kmalloc(sizeof(struct WorkingSetElement));
			newWSE->virtual_address = refEl->virtual_address;
			newWSE->empty = 0;
			LIST_INSERT_TAIL(&wsList, newWSE);           
        }		
	}
	return numOfFaults;
	//Comment the following line
	//panic("get_optimal_num_faults() is not implemented yet...!!");
}

void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
{
#if USE_KHEAP
	if (isPageReplacmentAlgorithmOPTIMAL())
	{
		//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Reference Stream
		//Your code is here
		
   cprintf("Optimal page replacement entered\n");
    fault_va = ROUNDDOWN(fault_va, PAGE_SIZE);

	if (fault_va == 0 || fault_va >= KERNEL_BASE)
{
    cprintf("OPTIMAL: warning: strange fault va %x\n", fault_va);
    env_exit();
}

    
    // [1] Keep track of Active WS (we use faulted_env->page_WS_list)
    cprintf("1 for VA %x\n", fault_va);
    
    // [2] Check if page is in memory (has a frame allocated)
    uint32 *ptr_table = NULL;
    struct FrameInfo *existing_frame = get_frame_info(faulted_env->env_page_directory, fault_va, &ptr_table);
    
    if (existing_frame != NULL) {
        // Page IS in memory but not present - just set present bit
        cprintf("2 for VA %x - Page in memory, setting present bit\n", fault_va);
        uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
        pt_set_page_permissions(faulted_env->env_page_directory, fault_va, perms | PERM_PRESENT, 0);
    } else {
        // Page not in memory - need to allocate and possibly read from disk
        cprintf("2 for VA %x - Page not in memory\n", fault_va);
        
        // [3] Check if faulted page already in Active WS
        struct WorkingSetElement *wse;
        bool found = 0;
        LIST_FOREACH(wse, &(faulted_env->page_WS_list)) {
            if (wse->virtual_address == fault_va) {
                found = 1;
                break;
            }
        }
        
        // [3] If page is already in Active WS, do nothing (shouldn't happen if frame is NULL)
        if (found) {
            cprintf("3 for VA %x - Already in WS (unexpected!)\n", fault_va);
        } else {
            // Page NOT in Active WS
            uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
            
            // If Active WS is FULL, reset present & delete all its pages
            if (wsSize >= faulted_env->page_WS_max_size) {
                cprintf("3 for VA %x - WS FULL, clearing all pages\n", fault_va);
                while (LIST_SIZE(&(faulted_env->page_WS_list)) > 0) {
                    struct WorkingSetElement *victimWSElement = LIST_FIRST(&(faulted_env->page_WS_list));
                    if (victimWSElement == NULL)
                        panic("optimal: no victim selected!");

                    uint32 permsPt = pt_get_page_permissions(faulted_env->env_page_directory, victimWSElement->virtual_address);
                    uint32 *victim_ptr_table;
                    struct FrameInfo *victim_frame = get_frame_info(faulted_env->env_page_directory, victimWSElement->virtual_address, &victim_ptr_table);
                    
                    if (victim_frame == NULL) {
                        panic("page_fault_handler: frame_info is NULL for the victim page!");
                    }
                    
                    if (permsPt & PERM_MODIFIED) {
                        pf_update_env_page(faulted_env, victimWSElement->virtual_address, victim_frame);
                    }
                    
                    unmap_frame(faulted_env->env_page_directory, victimWSElement->virtual_address);
                    LIST_REMOVE(&(faulted_env->page_WS_list), victimWSElement);
                    
                    if (faulted_env->page_last_WS_element == victimWSElement) {
                        faulted_env->page_last_WS_element = LIST_NEXT(victimWSElement);
                        if (faulted_env->page_last_WS_element == NULL) {
                            faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
                        }
                    }
                    kfree(victimWSElement);
                }
                faulted_env->page_last_WS_element = NULL;
            }

            // [4] Add the faulted page to the Active WS
            cprintf("4 for VA %x - Allocating frame and adding to WS\n", fault_va);
            struct FrameInfo *frame;
            if (allocate_frame(&frame) != 0) {
                panic("Optimal: allocate_frame failed");
            }

            struct WorkingSetElement *Element = env_page_ws_list_create_element(faulted_env, fault_va);
            map_frame(faulted_env->env_page_directory, frame, fault_va, PERM_PRESENT | PERM_WRITEABLE | PERM_USER);

            int faultPage = pf_read_env_page(faulted_env, (void*)fault_va);

            if (faultPage == E_PAGE_NOT_EXIST_IN_PF) {
                if (!(fault_va >= USTACKBOTTOM && fault_va < USTACKTOP) && 
                    !(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)) {
                    unmap_frame(faulted_env->env_page_directory, fault_va);
                    env_exit();
                }
            }

            // Recalculate wsSize after potential clearing
            wsSize = LIST_SIZE(&(faulted_env->page_WS_list));
            
            if (faulted_env->page_last_WS_element == NULL) {
                LIST_INSERT_TAIL(&(faulted_env->page_WS_list), Element);
                if (wsSize == faulted_env->page_WS_max_size - 1) {
                    faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
                }
            } else {
                LIST_INSERT_BEFORE(&(faulted_env->page_WS_list), faulted_env->page_last_WS_element, Element);
            }
        }
    }

    // [5] Append faulted page to the end of the reference stream list (ALWAYS)
    cprintf("5 for VA %x - Appending to reference stream\n", fault_va);
    struct PageRefElement *ref = (struct PageRefElement*)kmalloc(sizeof(struct PageRefElement));
    if (ref == NULL) {
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

				struct WorkingSetElement *wse;

				struct WorkingSetElement *start =faulted_env->page_last_WS_element;

				if (start == NULL) start = LIST_FIRST(&(faulted_env->page_WS_list));

				
				while(victimWSElement == NULL)
				{
					//trial
						wse = start;
						do {
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
							wse = LIST_NEXT(wse);
							if (wse == NULL)
								wse = LIST_FIRST(&(faulted_env->page_WS_list));
						}while(wse  != start);
					
				}

				faulted_env->page_last_WS_element = LIST_NEXT(victimWSElement);

				if (faulted_env->page_last_WS_element == NULL){
					faulted_env->page_last_WS_element =LIST_FIRST(&(faulted_env->page_WS_list));
				}

				if (victimWSElement == NULL)
				panic("clock: no victim selected!");

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

				if (start == NULL) start = LIST_FIRST(&(faulted_env->page_WS_list));

				
				while(victimWSElement == NULL)
				{
					//first trial 	
					wse = start;
					do {
						uint32 permsPt = pt_get_page_permissions(faulted_env->env_page_directory, wse->virtual_address);
						if (!(permsPt & PERM_USED) && !(permsPt & PERM_MODIFIED)) {
							victimWSElement = wse;
							break;
						}
						wse = LIST_NEXT(wse);
						if (wse == NULL)
							wse = LIST_FIRST(&(faulted_env->page_WS_list));
					}while(wse != start);
					//second trial
					if (victimWSElement == NULL ) {
						wse = start;
						do {
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
							wse = LIST_NEXT(wse);
							if (wse == NULL)
								wse = LIST_FIRST(&(faulted_env->page_WS_list));
						}while(wse  != start);
					}
				}

				faulted_env->page_last_WS_element = LIST_NEXT(victimWSElement);

				if (faulted_env->page_last_WS_element == NULL){
					faulted_env->page_last_WS_element =LIST_FIRST(&(faulted_env->page_WS_list));
				}

				if (victimWSElement == NULL)
				panic("modclock: no victim selected!");

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



