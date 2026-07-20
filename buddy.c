#include "buddy.h"
#include <stdint.h>
#include <string.h>

#define NULL ((void *)0)
#define MIN_RANK 1
#define MAX_RANK 16
#define PAGE_SIZE (4 * 1024)  // 4K bytes

typedef struct block {
    struct block *next;
    struct block *prev;
} block_t;

static void *base_addr = NULL;
static int total_pages = 0;
static block_t *free_lists[MAX_RANK + 2];
static uint8_t *page_alloc_rank = NULL;
static int free_count[MAX_RANK + 2];  // Track count of free blocks per rank

static inline int rank_to_pages(int rank) {
    return 1 << (rank - 1);
}

// Get buddy page index using XOR
static inline int get_buddy_page(int page_idx, int rank) {
    int size = rank_to_pages(rank);
    return page_idx ^ size;
}

static inline void* page_to_addr(int page_idx) {
    return (char*)base_addr + page_idx * PAGE_SIZE;
}

static inline int addr_to_page(void *p) {
    return (int)(((uintptr_t)p - (uintptr_t)base_addr) / PAGE_SIZE);
}

static inline int is_valid_addr(void *p) {
    if (base_addr == NULL || p == NULL) return 0;
    uintptr_t addr = (uintptr_t)p;
    uintptr_t base = (uintptr_t)base_addr;
    uintptr_t end = base + (uintptr_t)total_pages * PAGE_SIZE;
    return addr >= base && addr < end && ((addr - base) % PAGE_SIZE == 0);
}

static void remove_from_free_list(block_t *block, int rank) {
    if (block->prev != NULL) {
        block->prev->next = block->next;
    } else {
        free_lists[rank] = block->next;
    }
    if (block->next != NULL) {
        block->next->prev = block->prev;
    }
    free_count[rank]--;
}

static void add_to_free_list(block_t *block, int rank) {
    block->next = free_lists[rank];
    block->prev = NULL;
    if (free_lists[rank] != NULL) {
        free_lists[rank]->prev = block;
    }
    free_lists[rank] = block;
    free_count[rank]++;
}

static int coalesce_block(int page_idx, int rank) {
    while (rank < MAX_RANK) {
        int buddy_idx = get_buddy_page(page_idx, rank);
        
        // Fast check: is buddy_idx valid and free?
        if (buddy_idx < 0 || buddy_idx >= total_pages) {
            break;
        }
        
        // Look for buddy in free list - need to search since we don't have a hash table
        block_t *b = free_lists[rank];
        int found_buddy = 0;
        while (b != NULL) {
            if (addr_to_page(b) == buddy_idx) {
                found_buddy = 1;
                break;
            }
            b = b->next;
        }
        
        if (!found_buddy) {
            break;
        }
        
        // Remove buddy from free list
        remove_from_free_list((block_t*)page_to_addr(buddy_idx), rank);
        
        // Remove current from free list
        remove_from_free_list((block_t*)page_to_addr(page_idx), rank);
        
        // Merge - lower page index becomes the new block
        if (buddy_idx < page_idx) {
            page_idx = buddy_idx;
        }
        
        rank++;
        add_to_free_list((block_t*)page_to_addr(page_idx), rank);
    }
    
    return rank;
}

int init_page(void *p, int pgcount) {
    if (p == NULL || pgcount <= 0) return EINVAL;
    
    base_addr = p;
    total_pages = pgcount;
    
    memset(free_lists, 0, sizeof(free_lists));
    memset(free_count, 0, sizeof(free_count));
    
    static uint8_t rank_array[65536];
    page_alloc_rank = rank_array;
    memset(page_alloc_rank, 0, total_pages * sizeof(uint8_t));
    
    // Add all pages as rank 1 blocks
    for (int i = pgcount - 1; i >= 0; i--) {
        block_t *block = (block_t*)page_to_addr(i);
        block->next = free_lists[1];
        block->prev = NULL;
        if (free_lists[1] != NULL) {
            free_lists[1]->prev = block;
        }
        free_lists[1] = block;
    }
    free_count[1] = pgcount;
    
    return OK;
}

void *alloc_pages(int rank) {
    if (rank < MIN_RANK || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }
    
    // Find a free block of at least the requested rank
    int current_rank = rank;
    while (current_rank <= MAX_RANK && free_lists[current_rank] == NULL) {
        current_rank++;
    }
    
    if (current_rank > MAX_RANK) {
        return ERR_PTR(-ENOSPC);
    }
    
    // Split larger blocks
    while (current_rank > rank) {
        block_t *block = free_lists[current_rank];
        remove_from_free_list(block, current_rank);
        
        current_rank--;
        int page_idx = addr_to_page(block);
        int buddy_idx = page_idx + rank_to_pages(current_rank);
        
        // Add buddies - lower index first to maintain order
        add_to_free_list((block_t*)page_to_addr(buddy_idx), current_rank);
        block_t *b1 = (block_t*)page_to_addr(page_idx);
        b1->next = free_lists[current_rank];
        b1->prev = NULL;
        if (free_lists[current_rank] != NULL) {
            free_lists[current_rank]->prev = b1;
        }
        free_lists[current_rank] = b1;
        free_count[current_rank]++;
    }
    
    // Take from free list
    block_t *block = free_lists[rank];
    remove_from_free_list(block, rank);
    
    int page_idx = addr_to_page(block);
    int pages = rank_to_pages(rank);
    for (int i = 0; i < pages; i++) {
        page_alloc_rank[page_idx + i] = rank;
    }
    
    return block;
}

int return_pages(void *p) {
    if (!is_valid_addr(p)) {
        return -EINVAL;
    }
    
    int page_idx = addr_to_page(p);
    int rank = page_alloc_rank[page_idx];
    
    if (rank == 0) {
        return -EINVAL;
    }
    
    int pages = rank_to_pages(rank);
    for (int i = 0; i < pages; i++) {
        page_alloc_rank[page_idx + i] = 0;
    }
    
    add_to_free_list((block_t*)p, rank);
    coalesce_block(page_idx, rank);
    
    return OK;
}

int query_ranks(void *p) {
    if (!is_valid_addr(p)) {
        return -EINVAL;
    }
    
    int page_idx = addr_to_page(p);
    
    // Check if allocated
    if (page_alloc_rank[page_idx] != 0) {
        return page_alloc_rank[page_idx];
    }
    
    // Find in free lists
    for (int rank = 1; rank <= MAX_RANK; rank++) {
        block_t *block = free_lists[rank];
        while (block != NULL) {
            int blk_start = addr_to_page(block);
            int blk_pages = rank_to_pages(rank);
            if (page_idx >= blk_start && page_idx < blk_start + blk_pages) {
                return rank;
            }
            block = block->next;
        }
    }
    
    return 1;
}

int query_page_counts(int rank) {
    if (rank < MIN_RANK || rank > MAX_RANK) {
        return -EINVAL;
    }
    
    return free_count[rank];
}
