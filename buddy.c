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

static inline int rank_to_pages(int rank) {
    return 1 << (rank - 1);
}

// Get buddy using offset from base (0-indexed page number)
// For a block starting at page idx with given rank, return its buddy's page idx
static inline int get_buddy_page(int page_idx, int rank) {
    int size = rank_to_pages(rank);
    return page_idx ^ size;
}

// Convert page index to address
static inline void* page_to_addr(int page_idx) {
    return (char*)base_addr + page_idx * PAGE_SIZE;
}

// Convert address to page index
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
}

static void add_to_free_list(block_t *block, int rank) {
    block->next = free_lists[rank];
    block->prev = NULL;
    if (free_lists[rank] != NULL) {
        free_lists[rank]->prev = block;
    }
    free_lists[rank] = block;
}

static int coalesce_block(int page_idx, int rank) {
    while (rank < MAX_RANK) {
        int buddy_idx = get_buddy_page(page_idx, rank);
        void *buddy_addr = page_to_addr(buddy_idx);
        
        // Check if buddy is in free list of same rank
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
        remove_from_free_list((block_t*)buddy_addr, rank);
        
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
    
    for (int i = 0; i <= MAX_RANK + 1; i++) {
        free_lists[i] = NULL;
    }
    
    static uint8_t rank_array[65536];
    page_alloc_rank = rank_array;
    memset(page_alloc_rank, 0, total_pages * sizeof(uint8_t));
    
    // Add all pages as rank 1 blocks in reverse order
    for (int i = pgcount - 1; i >= 0; i--) {
        block_t *block = (block_t*)page_to_addr(i);
        add_to_free_list(block, 1);
    }
    
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
        
        // Add buddies to free list - lower index first so it's used
        add_to_free_list((block_t*)page_to_addr(buddy_idx), current_rank);
        add_to_free_list((block_t*)page_to_addr(page_idx), current_rank);
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
    
    int count = 0;
    block_t *block = free_lists[rank];
    while (block != NULL) {
        count++;
        block = block->next;
    }
    
    return count;
}
