/**
 * libjpegのメモリアロケーション実装
 */
#include "jinclude.h"
#include "jpeglib.h"
#include "jerror.h"
#include "jmemsys.h"		/* import the system-dependent declarations */
#include "mem_region_list.h"

/**
 * stdlibのmallocを使うかどうか。
 * 
 * 1: malloc()で16MiBを確保する。
 * 0: 固定領域を割り当てる。(MCU向け)
 */
#define USE_MEMALLOC (1)

/**
 * アロケーション/解放したときにメモリをダンプするかどうか
 */
#define DUMP_MEMORY (1)


#if USE_MEMALLOC
#include <stdlib.h>
#else
#define MEM_ADDR (mem_addr_t*)(0x00000000) 
#define MEM_SIZE (16 * 1024 * 1024) // 16Mib
#endif

#define MEM_REGION_COUNT (64)
static struct mem_region MemRegions[MEM_REGION_COUNT];
static mem_region_list_t MemRegionList;
static j_common_ptr UsedObject = NULL;

#if DUMP_MEMORY
static void dump_memories(void);
#endif

/**
 * メモリ確保を初期化する。
 */
long
jpeg_mem_init(j_common_ptr cinfo)
{
    if (UsedObject != NULL) {
        return 0;
    }
#if USE_MEMALLOC
    mem_size_t mem_size = 16 * 1024 * 1024;
    mem_addr_t mem_addr = (mem_addr_t)(malloc(mem_size));
    if (mem_addr == NULL) {
        return 0;
    }
#else
    mem_addr_t mem_addr = MEM_ADDR;
    mem_size_t mem_size = MEM_SIZE;
#endif

    mem_region_list_init(&MemRegionList, mem_addr, mem_size, MemRegions, MEM_REGION_COUNT);
    UsedObject = cinfo;
    return mem_region_list_get_free(&MemRegionList);
}

/**
 * メモリの確保を終了する。(クリーンアップする)
 */
void
jpeg_mem_term(j_common_ptr cinfo)
{
    if ((UsedObject != NULL) && (cinfo == UsedObject)) {
#if USE_MEMALLOC
        free(MemRegionList.free.address); // Cleanup heap.
#endif
        mem_region_list_destroy(&MemRegionList);
        UsedObject = NULL;
    }
    return;
}


 /*
  * Memory allocation and freeing are controlled by the regular library
  * routines malloc() and free().
  */

void*
jpeg_get_small(j_common_ptr cinfo, size_t sizeofobject)
{
    void* ret = NULL;
    if (cinfo == UsedObject) {
        ret = mem_region_list_assign(&MemRegionList, (mem_size_t)(sizeofobject));
#if DUMP_MEMORY
        if (ret != NULL) {
            printf("Allocate %d bytes. TotalUsed=%d/%d\n",
                (int)(sizeofobject),
                (int)(mem_region_list_get_used(&MemRegionList)),
                (int)(mem_region_list_get_used(&MemRegionList) + mem_region_list_get_free(&MemRegionList)));
        } else {
            dump_memories();
        }
#endif
    }
    return ret;
}

void
jpeg_free_small(j_common_ptr cinfo, void* object, size_t sizeofobject)
{
    if (cinfo == UsedObject) {
        mem_region_list_release(&MemRegionList, object);
#if DUMP_MEMORY
        printf("Release %d bytes. TotalUsed=%d/%d\n", 
            (int)(sizeofobject),
            (int)(mem_region_list_get_used(&MemRegionList)),
            (int)(mem_region_list_get_used(&MemRegionList) + mem_region_list_get_free(&MemRegionList)));
#endif
    }
}


/*
 * "Large" objects are treated the same as "small" ones.
 * NB: although we include FAR keywords in the routine declarations,
 * this file won't actually work in 80x86 small/medium model; at least,
 * you probably won't be able to process useful-size images in only 64KB.
 */

void*
jpeg_get_large(j_common_ptr cinfo, size_t sizeofobject)
{
    void* ret = NULL;
    if (cinfo == UsedObject) {
        ret = mem_region_list_assign(&MemRegionList, (mem_size_t)(sizeofobject));
#if DUMP_MEMORY
        if (ret != NULL) {
            printf("Allocate %d bytes. TotalUsed=%d/%d\n",
                (int)(sizeofobject),
                (int)(mem_region_list_get_used(&MemRegionList)),
                (int)(mem_region_list_get_used(&MemRegionList) + mem_region_list_get_free(&MemRegionList)));
        }
        else {
            dump_memories();
        }
#endif
    }
    return ret;
}

void
jpeg_free_large(j_common_ptr cinfo, void FAR* object, size_t sizeofobject)
{
    if (cinfo == UsedObject) {
        mem_region_list_release(&MemRegionList, object);
#if DUMP_MEMORY
        printf("Release %d bytes. TotalUsed=%d/%d\n",
            (int)(sizeofobject),
            (int)(mem_region_list_get_used(&MemRegionList)),
            (int)(mem_region_list_get_used(&MemRegionList) + mem_region_list_get_free(&MemRegionList)));
#endif
    }
}


/*
 * This routine computes the total memory space available for allocation.
 * Here we always say, "we got all you want bud!"
 */

long
jpeg_mem_available(j_common_ptr cinfo, long min_bytes_needed,
    long max_bytes_needed, long already_allocated)
{
    return max_bytes_needed;
}


/*
 * Backing store (temporary file) management.
 * Since jpeg_mem_available always promised the moon,
 * this should never be called and we can just error out.
 */

void
jpeg_open_backing_store(j_common_ptr cinfo, backing_store_ptr info,
    long total_bytes_needed)
{
    ERREXIT(cinfo, JERR_NO_BACKING_STORE);
}

#if DUMP_MEMORY
static void dump_memories(void) {
    mem_size_t used_size = mem_region_list_get_used(&MemRegionList);
    mem_size_t free_size = mem_region_list_get_free(&MemRegionList);
    printf("Used = %u\n", used_size);
    {
        const struct mem_region* pregion = MemRegionList.used.next;
        while (pregion != &(MemRegionList.used)) {
            printf("  %p %u\n", pregion->address, pregion->length);
            pregion = pregion->next;
        }
    }

    printf("Free = %u\n", free_size);
    {
        const struct mem_region* pregion = MemRegionList.free.next;
        while (pregion != &(MemRegionList.free)) {
            printf("  %p %u\n", pregion->address, pregion->length);
            pregion = pregion->next;
        }
    }
    printf("Total = %u\n", used_size + free_size);

    return;
}
#endif
