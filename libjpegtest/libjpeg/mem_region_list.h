#ifndef MEM_REGION_LIST_H
#define MEM_REGION_LIST_H

#include <stdint.h>
#include <stdbool.h>
#include "mem_region.h"


struct mem_region_list {
    struct mem_region used;
    struct mem_region free;
    struct mem_region *entries;
    int32_t entry_count;
};
typedef struct mem_region_list mem_region_list_t;

bool mem_region_list_init(mem_region_list_t *list, mem_addr_t address, mem_size_t length,
	struct mem_region *entries, int32_t entry_count);
void mem_region_list_destroy(mem_region_list_t *list);

mem_size_t mem_region_list_get_used(const mem_region_list_t *list);
mem_size_t mem_region_list_get_free(const mem_region_list_t *list);

mem_addr_t mem_region_list_assign(mem_region_list_t *list, mem_size_t length);
void mem_region_list_release(mem_region_list_t *list, mem_addr_t address);

#endif // MEM_REGION_LIST_H
