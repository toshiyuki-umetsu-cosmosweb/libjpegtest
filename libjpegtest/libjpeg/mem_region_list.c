#include <stddef.h>
#include "mem_region_list.h"

static struct mem_region* get_free_entry(mem_region_list_t* list);
static struct mem_region* find_insert_entry(struct mem_region* head, mem_addr_t address);
static void insert_entry(struct mem_region* prev_entry, struct mem_region* pentry);
static void release_entry(struct mem_region* pentry);
static void remove_entry(struct mem_region* pentry);
static uint32_t get_entry_count(const struct mem_region* head);
static struct mem_region* find_blank_region(struct mem_region* head, mem_size_t length);
static struct mem_region* find_region(struct mem_region* head, mem_addr_t address);
static void arrange_regions(struct mem_region* head);

static void init_lock(mem_region_list_t* list);
static void destroy_lock(mem_region_list_t* list);
static void lock(mem_region_list_t* list);
static void unlock(mem_region_list_t* list);


#define ALIGNMENT_SIZE (4u)

/**
 * Initialize memory region list.
 *
 * @param list A list to initialize region.
 * @param address Start address to assign list.
 * @param length Length of memory area.
 * @param entries Entris to use at this list.
 * @param entry_count Count of entries.
 * @retval true
 *           Succeed.
 * @retval false
 *           Failure. Invalid argument passed.
 */
bool mem_region_list_init(mem_region_list_t* list, mem_addr_t address, mem_size_t length,
    struct mem_region* entries, int32_t entry_count)
{
    if ((list == NULL) || (entries == NULL) || (entry_count < 2L))
    {
        return false;
    }
    list->entries = entries;
    list->entry_count = entry_count;
    for (int32_t i = 0L; i < entry_count; i++)
    {
        entries[i].address = NULL;
        entries[i].length = 0u;
        entries[i].prev = NULL;
        entries[i].next = NULL;
    }
    list->used.address = NULL;
    list->used.length = 0u;
    list->used.next = &(list->used);
    list->used.prev = &(list->used);
    list->free.address = address;
    list->free.length = length;
    list->free.next = &(list->free);
    list->free.prev = &(list->free);

    struct mem_region* pentry = get_free_entry(list);
    pentry->address = address;
    pentry->length = length;
    insert_entry(find_insert_entry(&(list->free), address), pentry);

    init_lock(list);

    return true;
}
/**
 * Destroy list.
 *
 * @param list A list object to destroy.
 */
void mem_region_list_destroy(mem_region_list_t* list)
{
    destroy_lock(list);

    list->used.address = NULL;
    list->used.length = 0u;
    list->free.address = NULL;
    list->free.length = 0u;
    list->entries = NULL;
    list->entry_count = 0u;

    return;
}

/**
 * Getting used size.
 *
 * @param list A list to get used size.
 * @retval Used size returned.
 */
mem_size_t mem_region_list_get_used(const mem_region_list_t* list)
{
    return (list != NULL) ? list->used.length : 0u;
}
/**
 * Getting free size.
 *
 * @param list A list to get free size.
 * @retval Free size returned.
 */
mem_size_t mem_region_list_get_free(const mem_region_list_t* list)
{
    return (list != NULL) ? list->free.length : 0u;
}

/**
 * Getting entry count.
 * 
 * @param list A list to get entry count.
 * @param pfree Variable to store free count. (If not need, set NULL)
 * @param pused Variable to store used count. (If not need, set NULL)
 */
void mem_region_list_get_entry_count(const mem_region_list_t* list, uint32_t* pfree, uint32_t* pused) {
    if (list == NULL) {
        return;
    }
    uint32_t used_count = get_entry_count(&list->used);
    if (pused != NULL) {
        (*pused) = used_count;
    }
    if (pfree != NULL) {
        (*pfree) = list->entry_count - used_count;
    }
    
    return;
}


/**
 * Assign region.
 */
mem_addr_t mem_region_list_assign(mem_region_list_t* list, mem_size_t length)
{
    if ((list == NULL) || (length <= 0u)) {
        return NULL;
    }

    mem_size_t needs_length = ((length + ALIGNMENT_SIZE - 1) / ALIGNMENT_SIZE) * ALIGNMENT_SIZE;

    mem_addr_t ret = NULL;

    lock(list);

    struct mem_region* pentry = get_free_entry(list);
    if (pentry == NULL) { // No left region entry?
        ret = NULL;
    }
    else {
        struct mem_region* blank_region_entry = find_blank_region(&(list->free), needs_length);
        if (blank_region_entry == NULL) { // No left region?
            ret = NULL;
        }
        else {
            // Assign memory.
            pentry->address = blank_region_entry->address;
            pentry->length = needs_length;
            blank_region_entry->address += needs_length;
            blank_region_entry->length -= needs_length;

            if (blank_region_entry->length <= 0u) {
                release_entry(blank_region_entry);
            }

            list->free.length -= needs_length;
            list->used.length += needs_length;

            insert_entry(find_insert_entry(&(list->used), pentry->address), pentry);

            ret = pentry->address;
        }
    }

    unlock(list);

    return ret;
}

/**
 * Release region.
 *
 * @param list Region list.
 * @param address Release region address.
 */
void mem_region_list_release(mem_region_list_t* list, mem_addr_t address)
{
    if (list == NULL) {
        return;
    }

    lock(list);

    struct mem_region* pentry = find_region(&(list->used), address);
    if (pentry == NULL) { // Target entry exists?
        // do nothing.
    }
    else {
        remove_entry(pentry);

        insert_entry(find_insert_entry(&(list->free), pentry->address), pentry);

        list->free.length += pentry->length;
        list->used.length -= pentry->length;

        arrange_regions(&(list->free));
    }

    unlock(list);
}

/**
 * Get unused region_entry.
 *
 * @param list A list object which have entries.
 * @retval Non NULL
 *           Unused entry object.
 * @retval NULL
 *           Unused object is not exists.
 */
static struct mem_region* get_free_entry(mem_region_list_t* list)
{
    struct mem_region* region = list->entries;
    for (int32_t i = 0L; i < list->entry_count; i++)
    {
        if (region->address == NULL)
        {
            return region;
        }
        region++;
    }

    return NULL;
}

/**
 * Find insert position.
 *
 * @param head A head object of region entry list
 * @param address Address
 * @retval A region entry object to insert to previous.
 */
static struct mem_region* find_insert_entry(struct mem_region* head, mem_addr_t address)
{
    struct mem_region* pentry = head->next;
    while (pentry != head) {
        if (address < pentry->address) { // address is larger than pentry's address.
            // Insert this position.
            return pentry;
        }
        pentry = pentry->next;
    }
    // Insert to tail.
    return head;
}

/**
 * Insert pentry to previous position of prev_entry.
 *
 * @param prev_entry Previous positioning entry object to insert.
 * @param pentry Entry object to insert.
 */
static void insert_entry(struct mem_region* prev_entry, struct mem_region* pentry)
{
    prev_entry->prev->next = pentry;
    pentry->prev = prev_entry->prev;
    pentry->next = prev_entry;
    prev_entry->prev = pentry;

    return;
}
/**
 * Release region entry object.
 *
 * @param pentry Entry object to release.
 */
static void release_entry(struct mem_region* pentry)
{
    remove_entry(pentry);

    pentry->address = NULL;
    pentry->length = 0u;

    return;
}
/**
 * Remove region entry object from list.
 *
 * @param pentry Entry object to remove.
 */
static void remove_entry(struct mem_region* pentry)
{
    pentry->next->prev = pentry->prev;
    pentry->prev->next = pentry->next;

    pentry->next = pentry;
    pentry->prev = pentry;

    return;
}

/**
 * Getting entry count of list.
 * 
 * @param head Head entry of list.
 * @retval Number of entry count returned.
 */
static uint32_t get_entry_count(const struct mem_region* head) 
{
    uint32_t count = 0;
    const struct mem_region* pentry = head->next;
    while (pentry != head) {
        count++;
        pentry = pentry->next;
    }
    return count;
}

/**
 * Find blank region which has a enougth space to assign specified length.
 *
 * @param head Head entry of free list.
 * @param length Length to need.
 * @retval NULL
 *           No space left on list.
 * @retval Non-NULL
 *           A entry object which has enough size.
 */
static struct mem_region* find_blank_region(struct mem_region* head, mem_size_t length)
{
    struct mem_region* pentry = head->next;
    while (pentry != head) {
        if (pentry->length >= length) {
            return pentry;
        }
        pentry = pentry->next;
    }
    return NULL;
}
/**
 * Find region which has indicate address.
 *
 * @param head Head of region entry array.
 * @param address Address.
 * @retval NULL
 *           Region entry not found.
 * @retval Non-NULL
 *           Region entry object which has specified address.
 */
static struct mem_region* find_region(struct mem_region* head, mem_addr_t address)
{
    struct mem_region* pentry = head->next;
    while (pentry != head) {
        if (pentry->address == address) {
            return pentry;
        }
        pentry = pentry->next;
    }
    return NULL;
}

/**
 * Arrange region entry array. Combine continous two regions.
 */
static void arrange_regions(struct mem_region* head)
{
    struct mem_region* pentry = head->next;
    while (pentry != head) {
        struct mem_region* pnext_entry = pentry->next;
        while ((pnext_entry != head) // Next entry is exists?
            && ((pentry->address + pentry->length) == pnext_entry->address)) { // Next address is continuous?
            pentry->length += pnext_entry->length;
            release_entry(pnext_entry);
            pnext_entry = pentry->next;
        }
        pentry = pentry->next;
    }
}

/**
 * Initialize lock object.
 *
 * @param list Region list.
 */
static void init_lock(mem_region_list_t* list)
{
    // Note : If lock function available, implements here.
}
/**
 * Destroy lock object.
 *
 * @param list Regin list.
 */
static void destroy_lock(mem_region_list_t* list)
{
    // Note : If lock function available, implements here.
}
/**
 * Lock list.
 *
 * @param list Region list.
 */
static void lock(mem_region_list_t* list)
{
    // Note : If lock function available, implements here.
}
/**
 * Unlock list.
 *
 * @param list Region list.
 */
static void unlock(mem_region_list_t* list)
{
    // Note : If lock function available, implements here.
}
