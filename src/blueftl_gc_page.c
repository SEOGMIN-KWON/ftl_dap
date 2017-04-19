#ifdef KERNEL_MODE

#include <time.h>
#include <limits.h>
#include <linux/vmalloc.h>

#else

#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#endif

#include "blueftl_gc_page.h"
#include "blueftl_ftl_base.h"
#include "blueftl_mapping_page.h"
#include "blueftl_util.h"
#include "blueftl_user_vdevice.h"
#include "blueftl_wl_dual_pool.h"

int gd=0;

struct flash_block_t *victim_random(struct flash_ssd_t *ptr_ssd){
	uint32_t bus;
	uint32_t chip;
	uint32_t block;
	
	do{
		bus = rand()%ptr_ssd->nr_buses;
		chip = rand()%ptr_ssd->nr_chips_per_bus;
		block = rand()%ptr_ssd->nr_blocks_per_chip;
		// printf("%d ",block);
	}while(ptr_ssd->list_buses[bus].list_chips[chip].list_blocks[block].is_reserved_block || 
		ptr_ssd->list_buses[bus].list_chips[chip].list_blocks[block].nr_valid_pages == ptr_ssd->nr_pages_per_block
	);
	return &(ptr_ssd->list_buses[bus].list_chips[chip].list_blocks[block]);
}

// b = 0: greedy
// b = 1: cost-benefit
struct flash_block_t *compare_block(struct flash_ssd_t *ptr_ssd, struct flash_block_t *b1, struct flash_block_t *b2, short select){
	if(select){
		uint32_t cur_time = timer_get_timestamp_in_us();
		double u1 = b1->nr_valid_pages/ptr_ssd->nr_pages_per_block;
		double u2 = b2->nr_valid_pages/ptr_ssd->nr_pages_per_block;

		double f1 = u1/((1 - u1) * (cur_time - b1->last_modified_time));
		double f2 = u2/((1 - u2) * (cur_time - b2->last_modified_time));

		if(f1 < f2){ return b1; } 
		else { return b2; }
	} else {
		if(b1->nr_invalid_pages > b2->nr_invalid_pages){ return b1; } 
		else { return b2; }
	}
}

//reserved block 한개 넣음 

// select 0:random, 1:greedy, 2:costbenefit
struct flash_block_t *select_victim(struct flash_ssd_t *ptr_ssd, uint32_t bus, uint32_t chip, short select){
	if(!select) return victim_random(ptr_ssd);
	uint32_t block = 0;
	struct flash_block_t *victim = &(ptr_ssd->list_buses[bus].list_chips[chip].list_blocks[block]);

	if(victim->is_reserved_block) { 
		block++;
		victim = &(ptr_ssd->list_buses[bus].list_chips[chip].list_blocks[block]);
	}

	for(block++; block < ptr_ssd->nr_blocks_per_chip; block++){
		if(ptr_ssd->list_buses[bus].list_chips[chip].list_blocks[block].is_reserved_block) continue;
		victim = compare_block(ptr_ssd, victim, &(ptr_ssd->list_buses[bus].list_chips[chip].list_blocks[block]), select-1);
	}

	return victim;
}


void move_block(struct ftl_context_t *context, struct flash_block_t *src, struct flash_block_t *dest){
	// 1.src의 데이터를 버퍼로 옮김
	// 2.버퍼를 dest로 복사
	// 3.매핑테이블 변경
	struct flash_ssd_t *ptr_ssd = context->ptr_ssd;
	char *buf = (char *)malloc(context->ptr_vdevice->page_main_size * ptr_ssd->nr_pages_per_block);
	struct ftl_page_mapping_context_t* ptr_pg_mapping = (struct ftl_page_mapping_context_t*)context->ptr_mapping;
	uint32_t src_pg, dest_pg;
	uint32_t cnt=0;
	//printf("-S---- %s\n", __func__);
	//printf("dest valid %d\n", dest->nr_valid_pages);
	for(src_pg=0, dest_pg=0; src_pg < ptr_ssd->nr_pages_per_block; src_pg++){
		// printf("%d", src->list_pages[src_pg].page_status);
		if(	src->list_pages[src_pg].page_status == PAGE_STATUS_INVALID || // get src valid page
			src->list_pages[src_pg].page_status == PAGE_STATUS_FREE) { 
			// printf("c ");
			continue; 
		}
		// printf(" ");
		// get dest free page
		while(dest->list_pages[dest_pg].page_status != PAGE_STATUS_FREE) { dest_pg++; }
		// printf("gccnt %d\n",cnt);

		blueftl_user_vdevice_page_read(
			context->ptr_vdevice, 
			src->no_bus, 
			src->no_chip, 
			src->no_block, 
			src_pg, 
			context->ptr_vdevice->page_main_size, 
			buf
		);

		blueftl_user_vdevice_page_write( 
			context->ptr_vdevice,
			dest->no_bus, 
			dest->no_chip, 
			dest->no_block, 
			dest_pg, 
			context->ptr_vdevice->page_main_size, 
			buf
		);

		dest->list_pages[dest_pg].page_status = PAGE_STATUS_VALID;
		dest->nr_free_pages--;
		dest->nr_valid_pages++;	
		dest->list_pages[dest_pg].no_logical_page_addr = src->list_pages[src_pg].no_logical_page_addr;
		ptr_pg_mapping->ptr_pg_table[src->list_pages[src_pg].no_logical_page_addr] = 
			ftl_convert_to_physical_page_address(dest->no_bus, dest->no_chip, dest->no_block, dest_pg);
		cnt++;
	}
	// if(gd) printf("(%d)",cnt);
	free(buf);

	//printf("enddest valid %d\n", dest->nr_valid_pages);
	//printf("-E---- %s\n", __func__);
}

void gc_page_trigger_init(struct ftl_context_t *ptr_ftl_context){
	struct flash_ssd_t *ptr_ssd = ptr_ftl_context->ptr_ssd;
	// struct flash_block_t *reserved = ((struct ftl_page_mapping_context_t *)ptr_ftl_context->ptr_mapping)->reserved;

	if(ptr_ssd->list_buses[0].list_chips[0].nr_free_blocks>0){
		((struct ftl_page_mapping_context_t *)ptr_ftl_context->ptr_mapping)->reserved = ssdmgmt_get_free_block(ptr_ssd, 0, 0);
		ptr_ssd->list_buses[0].list_chips[0].nr_free_blocks--;
		ptr_ssd->list_buses[0].list_chips[0].nr_dirty_blocks++;
	}
	((struct ftl_page_mapping_context_t *)ptr_ftl_context->ptr_mapping)->reserved->is_reserved_block=1;
	//printf("init reserved %p\n", reserved);
}


int32_t gc_page_trigger_gc(struct ftl_context_t *ptr_ftl_context, int32_t bus, int32_t chip)
{
	struct flash_ssd_t *ptr_ssd = ptr_ftl_context->ptr_ssd;
	struct virtual_device_t *pvdevice = ptr_ftl_context->ptr_vdevice;
	uint32_t page_offset;
	bus=0;chip=0;
	struct flash_block_t *victim = select_victim(ptr_ssd, bus, chip, 1);
	struct flash_block_t *temp;
	struct flash_block_t *reserved = ((struct ftl_page_mapping_context_t *)ptr_ftl_context->ptr_mapping)->reserved;
	//printf("-S----- %s\n", __func__);
	//printf("freeblocks %d\n", ptr_ssd->list_buses[bus].list_chips[chip].nr_free_blocks);
	// uint32_t bb;
	// struct flash_chip_t c = ptr_ssd->list_buses[bus].list_chips[chip];
	// for(bb=0; bb<1024; bb++){
	// 	uint32_t v = c.list_blocks[bb].nr_valid_pages;
	// 	//printf("%d ", v);
	// }
	//printf("\n");

	//printf("dirties %d\n", ptr_ssd->list_buses[0].list_chips[0].nr_dirty_blocks);
	uint32_t o;

	// if(victim->nr_valid_pages<0) gd=1;
	// else gd=0;
	// if(gd) printf("[ %p,%2d,%2d,%2d | %p,%2d ]-->", victim, victim->nr_free_pages, victim->nr_invalid_pages, victim->nr_valid_pages, reserved, reserved->nr_valid_pages+reserved->nr_invalid_pages+reserved->nr_free_pages);

	// for(o=0; o<ptr_ssd->nr_pages_per_block; o++){
	// 	printf("%d ",reserved->list_pages[o].page_status);
	// }
	// printf("\n");
	//printf("victim %d | reserved %d\n", victim->nr_valid_pages, reserved->nr_valid_pages);
	// int o;
	// for(o=0; o<64; o++) printf("%d ", reserved->list_pages[o].page_status);
	//printf("\nbefore\n");
	move_block(ptr_ftl_context, victim, reserved);

	/* erase victim block */
	blueftl_user_vdevice_block_erase(pvdevice, victim->no_bus, victim->no_chip, victim->no_block);
	for (page_offset = 0; page_offset < ptr_ssd->nr_pages_per_block; page_offset++) {
		victim->list_pages[page_offset].page_status = PAGE_STATUS_FREE;
	}
	// ptr_ssd->list_buses[victim->no_bus].list_chips[victim->no_chip].nr_free_blocks++;
	// ptr_ssd->list_buses[reserved->no_bus].list_chips[reserved->no_chip].nr_dirty_blocks--;
	
	
	reserved->is_reserved_block = 0;
	//swap
	temp = reserved;
	((struct ftl_page_mapping_context_t *)ptr_ftl_context->ptr_mapping)->reserved = victim;
	reserved = ((struct ftl_page_mapping_context_t *)ptr_ftl_context->ptr_mapping)->reserved;
	victim = temp;
	
	reserved->is_reserved_block = 1; // victim이 reserved가 됨 (비어있는), 따라서 reserved에 valid가 

	reserved->nr_erase_cnt++;
	reserved->nr_recent_erase_cnt++;

	reserved->nr_valid_pages = 0;
	reserved->nr_invalid_pages = 0;
	reserved->nr_free_pages = ptr_ssd->nr_pages_per_block;
	reserved->last_modified_time = timer_get_timestamp_in_us();
	
	for(o=0; o<ptr_ssd->nr_pages_per_block; o++){
		if(reserved->list_pages[o].page_status==PAGE_STATUS_VALID) reserved->nr_valid_pages++; 
	}

	// if(gd) printf("[ %2d %2d %2d| %2d ]\n ", reserved->nr_free_pages, reserved->nr_invalid_pages, reserved->nr_valid_pages, victim->nr_valid_pages);

	// printf("after dirties %d\n", ptr_ssd->list_buses[0].list_chips[0].nr_dirty_blocks);
	// printf("reserved valid %d\n", reserved->nr_valid_pages);

	// for(o=0; o<64; o++) printf("%d ", reserved->list_pages[o].page_status);
	//printf("-E----- %s\n", __func__);

#if WL_ON
	update_max_min_nr_erase_cnt_in_pool(ptr_ftl_context);
	cold_data_migration(ptr_ftl_context);
	cold_pool_adjustment(ptr_ftl_context);
	hot_pool_adjustment(ptr_ftl_context);
#endif
	return 0;
}
