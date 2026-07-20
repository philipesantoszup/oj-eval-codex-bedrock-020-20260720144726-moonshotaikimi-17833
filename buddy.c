#include "buddy.h"
#include <stdint.h>
#include <string.h>

#define NULL ((void *)0)
#define MIN_RANK 1
#define MAX_RANK 16
#define PAGE_SIZE (4 * 1024)

typedef struct block {
    struct block *next;
    struct block *prev;
} block_t;

static void *base_addr = NULL;
static int total_pages = 0;
static block_t *free_lists[MAX_RANK + 2];
static uint8_t *page_alloc_rank = NULL;
static int free_count[MAX_RANK + 2];
static uint8_t *page_is_free = NULL;
// Track the rank of free blocks for O(1) query_ranks
static uint8_t *page_free_rank = NULL;

static inline int rank_to_pages(int rank) {
    return 1 << (rank - 1);
}

static inline int get_buddy_page(int page_idx, int rank) {
    return page_idx ^ rank_to_pages(rank);
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
        
        if (buddy_idx < 0 || buddy_idx >= total_pages) {
            break;
        }
        
        if (!page_is_free[buddy_idx] || page_free_rank[buddy_idx] != rank) {
            break;
        }
        
        block_t *b = free_lists[rank];
        while (b != NULL && addr_to_page(b) != buddy_idx) {
            b = b->next;
        }
        
        if (b == NULL) {
            break;
        }
        
        page_is_free[page_idx] = 0;
        page_is_free[buddy_idx] = 0;
        
        remove_from_free_list((block_t*)page_to_addr(buddy_idx), rank);
        remove_from_free_list((block_t*)page_to_addr(page_idx), rank);
        
        if (buddy_idx < page_idx) {
            page_idx = buddy_idx;
        }
        
        rank++;
        add_to_free_list((block_t*)page_to_addr(page_idx), rank);
        page_is_free[page_idx] = 1;
        page_free_rank[page_idx] = rank;
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
    static uint8_t free_array[65536];
    static uint8_t free_rank_array[65536];
    page_alloc_rank = rank_array;
    page_is_free = free_array;
    page_free_rank = free_rank_array;
    
    memset(page_alloc_rank, 0, total_pages * sizeof(uint8_t));
    memset(page_is_free, 0, total_pages * sizeof(uint8_t));
    memset(page_free_rank, 0, total_pages * sizeof(uint8_t));
    
    for (int i = pgcount - 1; i >= 0; i--) {
        block_t *block = (block_t*)page_to_addr(i);
        block->next = free_lists[1];
        block->prev = NULL;
        if (free_lists[1] != NULL) {
            free_lists[1]->prev = block;
        }
        free_lists[1] = block;
        page_is_free[i] = 1;
        page_free_rank[i] = 1;
    }
    free_count[1] = pgcount;
    
    return OK;
}

void *alloc_pages(int rank) {
    if (rank < MIN_RANK || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }
    
    int current_rank = rank;
    while (current_rank <= MAX_RANK && free_lists[current_rank] == NULL) {
        current_rank++;
    }
    
    if (current_rank > MAX_RANK) {
        return ERR_PTR(-ENOSPC);
    }
    
    while (current_rank > rank) {
        block_t *block = free_lists[current_rank];
        remove_from_free_list(block, current_rank);
        
        current_rank--;
        int page_idx = addr_to_page(block);
        int buddy_idx = page_idx + rank_to_pages(current_rank);
        
        page_is_free[page_idx] = 0;
        page_free_rank[page_idx] = 0;
        
        // Add buddy2 first
        block_t *b2 = (block_t*)page_to_addr(buddy_idx);
        b2->next = free_lists[current_rank];
        b2->prev = NULL;
        if (free_lists[current_rank] != NULL) {
            free_lists[current_rank]->prev = b2;
        }
        free_lists[current_rank] = b2;
        free_count[current_rank]++;
        page_is_free[buddy_idx] = 1;
        page_free_rank[buddy_idx] = current_rank;
        
        // Add buddy1
        block_t *b1 = (block_t*)page_to_addr(page_idx);
        b1->next = free_lists[current_rank];
        b1->prev = NULL;
        if (free_lists[current_rank] != NULL) {
            free_lists[current_rank]->prev = b1;
        }
        free_lists[current_rank] = b1;
        free_count[current_rank]++;
        page_is_free[page_idx] = 1;
        page_free_rank[page_idx] = current_rank;
    }
    
    block_t *block = free_lists[rank];
    remove_from_free_list(block, rank);
    
    int page_idx = addr_to_page(block);
    int pages = rank_to_pages(rank);
    for (int i = 0; i < pages; i++) {
        page_alloc_rank[page_idx + i] = rank;
    }
    page_is_free[page_idx] = 0;
    page_free_rank[page_idx] = 0;
    
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
    page_is_free[page_idx] = 1;
    page_free_rank[page_idx] = rank;
    coalesce_block(page_idx, rank);
    
    return OK;
}

int query_ranks(void *p) {
    if (!is_valid_addr(p)) {
        return -EINVAL;
    }
    
    int page_idx = addr_to_page(p);
    
    if (page_alloc_rank[page_idx] != 0) {
        return page_alloc_rank[page_idx];
    }
    
    // O(1) lookup using page_free_rank
    if (page_is_free[page_idx]) {
        return page_free_rank[page_idx];
    }
    
    return 1;
}

int query_page_counts(int rank) {
    if (rank < MIN_RANK || rank > MAX_RANK) {
        return -EINVAL;
    }
    
    return free_count[rank];
}
