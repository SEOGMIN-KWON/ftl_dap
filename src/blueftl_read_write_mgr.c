#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h> //  our new library 
#include <unistd.h>
#include <math.h>

#include "blueftl_user.h"
#include "blueftl_user_vdevice.h"
#include "blueftl_util.h"
#include "blueftl_ftl_base.h"
#include "blueftl_mapping_page.h"
#include "blueftl_mapping_block.h"
#include "blueftl_char.h"
#include "blueftl_wl_dual_pool.h"
#include "blueftl_gc_page.h"
#include "blueftl_read_write_mgr.h"


void blueftl_page_read(
    struct ftl_base_t ftl_base, 
    struct ftl_context_t *ptr_ftl_context, 
    uint32_t lpa_curr, 
    uint8_t *ptr_lba_buff
){
	uint32_t bus, chip, page, block;

	if (ftl_base.ftl_get_mapped_physical_page_address(ptr_ftl_context, lpa_curr, &bus, &chip, &block, &page) == 0)
	{
		
		blueftl_user_vdevice_page_read (_ptr_vdevice, bus, chip, block, page, _ptr_vdevice->page_main_size, (char*)ptr_lba_buff);

	} else {
		/* the requested logical page is not mapped to any physical page */
		/* simply ignore */
	}
}

uint32_t blueftl_page_write(
    struct ftl_base_t ftl_base, 
    struct ftl_context_t *ptr_ftl_context, 
    uint32_t lpa_curr, 
    uint8_t *ptr_lba_buff
){
    uint32_t bus, chip, block, page;
    uint8_t is_merge_needed = 0;
    
    /* get the new physical page address from the FTL */
    if (ftl_base.ftl_get_free_physical_page_address (
            ptr_ftl_context, lpa_curr, &bus, &chip, &block, &page) == -1) {
        /* there are no free pages; do garbage collection */
        if (ftl_base.ftl_trigger_gc != NULL) {

            /* trigger gc */
            if (ftl_base.ftl_trigger_gc (ptr_ftl_context, bus, chip) == -1) {
                printf ("bluessd: oops! garbage collection failed.\n");
                return -1;
            }

            /* garbage collection has been finished; chooses the new free page */
            if (ftl_base.ftl_get_free_physical_page_address (
                    ptr_ftl_context, lpa_curr, &bus, &chip, &block, &page) == -1) {
                printf ("bluessd: there is not sufficient space in flash memory.\n");
                return -1;
            }

            /* ok. we obtain new free space */
        } else if (ftl_base.ftl_trigger_merge != NULL) {
            /* the FTL does not support gc,
                so it is necessary to merge the new data with the existing data */
            is_merge_needed = 1;
        } else {
            printf ("bluessd: garbage collection is not registered\n");
            return -1;
        }
    }

    if (is_merge_needed == 0) {

        /* perform a page write */
        blueftl_user_vdevice_page_write (
            _ptr_vdevice, 
            bus, chip, block, page, 
            _ptr_vdevice->page_main_size, 
            (char*)ptr_lba_buff);

        /* map the logical address to the new physical address */
        if (ftl_base.ftl_map_logical_to_physical (
                ptr_ftl_context, lpa_curr, bus, chip, block, page) == -1) {
            printf ("bluessd: map_logical_to_physical failed\n");
            return -1;
        }
    } else {
        /* trigger merge with new data */
        if (ftl_base.ftl_trigger_merge (
                ptr_ftl_context, lpa_curr, ptr_lba_buff, bus, chip, block) == -1) {
            printf ("bluessd: block merge failed\n");
            return -1;
        }
    }

    return 0;
}