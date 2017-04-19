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
#include "lzrw3.h"

uint32_t _page_size;

uint8_t *_write_page_buff;
uint8_t *_compressed_buff;
uint32_t _nr_buff_pages;
uint32_t _write_page_buff_size;


void clean_buff(){
    _arr_lpa[0] = _arr_lpa[1] = _arr_lpa[2] = _arr_lpa[3] = -1;
    _nr_buff_pages = 0;
}

uint32_t blueftl_read_write_mgr_init(uint32_t page_size){   
    _page_size = page_size;
    clean_buff();
    // 4*(sz uint32_t) + (4 * page_size) 만큼의 write buffer를 만듦
    // 앞은 lpa 저장할 곳임 
    _write_page_buff_size = 4*sizeof(uint32_t) + (4*page_size)*sizeof(uint8_t);
    _write_page_buff = (uint8_t *)malloc(_write_page_buff_size);
    _compressed_buff = (uint8_t *)malloc(8*page_size*sizoef(uint8_t));
    return _write_page_buff | _compressed_buff; 
}

void blueftl_read_write_mgr_close(){
    free(write_page_buff);
}

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
    uint32_t *ptr_write_buff_data;
    uint32_t *ptr_write_buff_lpa;
    /*
        (1) write buff count 증가
        (2) 버퍼 lpa position 설정
        (3) lpa 저장
        (3) 버퍼 dest position 설정
        (4) write 대상 데이터를 버퍼에 복사
    */
    _nr_buff_pages++; // (1)
    ptr_write_buff_lpa = _write_page_buff + (_nr_buff_pages - 1)*sizeof(uint32_t); // (2)
    *ptr_write_buff_lpa = lpa_curr; // (3)
    ptr_write_buff = _write_page_buff + 4*sizeof(uint32_t) + ((_nr_buff_pages-1)*_page_size)); //(4)
    memcpy(ptr_write, ptr_lba_buff, _page_size); // (5)

    /* buff가 꽉찼으므로 compression 후 write */
    if(_nr_buff_pages == 4){
        /*
            압축이 필요한가 - 압축이 필요할 때
                1. 압축
                2. 압축된 페이지 사이즈에 맞게 ftl 페이지 요청
                3. phsical write
                4. ftl 등록
            압축이 필요한가 - 압축이 필요하지 않을 때
                각 4개의 페이지에 대해
                    1. ftl 페이지요청
                    2. physical write
                    3. ftl 등록 
            170419: 압축 write를 먼저 구현하자. 압축필요여부는 나중에.
            
            QUESTION
            Write를 했다 -> buffer에 들어가 있음
            buffer에서 꺼내 압축 하여 physical write를 하기 전에 read요청이 들어오면 어떻게 할것인가.
            =>  buffer에 있는 데이터를 그냥 꺼내라
        */
        
        /* 압축 */
        UWORD compressed_size = compress(_write_page_buff, _write_page_buff_size, _compressed_buff);
        // 압축된 페이지 계산
        uint32_t nr_compress_pages = compressed_size/page_size + compressed_size%page_size?1:0;
        
    }
    
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