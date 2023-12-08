#ifndef MEM_REGION_H
#define MEM_REGION_H

#include <stdint.h>

typedef uint8_t* mem_addr_t;
typedef uint32_t mem_size_t;

struct mem_region {
    mem_addr_t address; // Address.
    mem_size_t length; // Length.
    struct mem_region *prev; // Previous entry.
    struct mem_region *next; // Next entry.
};

#endif /* MEM_REGION_H */
