#ifndef _BLUESSD_FTL_RW_MGR
#define _BLUESSD_FTL_RW_MGR

void blueftl_page_read(
    struct ftl_base_t ftl_base, 
    struct ftl_context_t *ptr_ftl_context, 
    uint32_t lpa_curr, 
    uint8_t *ptr_lba_buff
);


uint32_t blueftl_page_write(
    struct ftl_base_t ftl_base, 
    struct ftl_context_t *ptr_ftl_context, 
    uint32_t lpa_curr, 
    uint8_t *ptr_lba_buff
);

#endif