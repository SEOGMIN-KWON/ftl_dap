#ifndef _BLUESSD_GC_PAGE
#define _BLUESSD_GC_PAGE

#include <linux/types.h>
#include "blueftl_ftl_base.h"

int32_t gc_page_trigger_gc(
	struct ftl_context_t *ptr_ftl_context, 
	int32_t gc_target_bus, 
	int32_t gc_target_chip);

void gc_page_trigger_init(struct ftl_context_t *ptr_ftl_context);

#endif
