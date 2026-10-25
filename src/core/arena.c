#include "core/arena.h"

#include <stdint.h>
#include <string.h>

#include "core/checked.h"
#include "core/mem.h"

struct wc_arena_block {
    wc_arena_block *next;
    unsigned char *data;
    size_t cap;
    size_t used;
};

#define WC_ARENA_DEFAULT_BLOCK (64u * 1024u)

static wc_status arena_new_block(wc_arena *arena, size_t min_cap) {
    size_t cap = arena->block_size;
    if (cap < min_cap) {
        cap = min_cap;
    }
    wc_arena_block *block = wc_mem_malloc(sizeof *block);
    if (block == NULL) {
        return WC_ENOMEM;
    }
    block->data = wc_mem_malloc(cap);
    if (block->data == NULL) {
        wc_mem_free(block);
        return WC_ENOMEM;
    }
    block->next = arena->head;
    block->cap = cap;
    block->used = 0;
    arena->head = block;
    arena->current = block;
    return WC_OK;
}

void wc_arena_init(wc_arena *arena, size_t block_size) {
    if (arena == NULL) {
        return;
    }
    arena->head = NULL;
    arena->current = NULL;
    arena->block_size = block_size == 0 ? WC_ARENA_DEFAULT_BLOCK : block_size;
    arena->live = 0;
    arena->high_water = 0;
}

void wc_arena_destroy(wc_arena *arena) {
    if (arena == NULL) {
        return;
    }
    wc_arena_block *block = arena->head;
    while (block != NULL) {
        wc_arena_block *next = block->next;
        wc_mem_free(block->data);
        wc_mem_free(block);
        block = next;
    }
    wc_arena_init(arena, arena->block_size);
}

void wc_arena_reset(wc_arena *arena) {
    if (arena == NULL) {
        return;
    }
    for (wc_arena_block *block = arena->head; block != NULL; block = block->next) {
        block->used = 0;
    }
    arena->current = arena->head;
    arena->live = 0;
}

WC_NODISCARD wc_status wc_arena_alloc(wc_arena *arena, size_t size, size_t align, void **out) {
    if (arena == NULL || out == NULL) {
        return WC_EINVAL;
    }
    *out = NULL;
    if (size == 0) {
        return WC_OK;
    }
    if (align == 0) {
        align = _Alignof(max_align_t);
    }
    if ((align & (align - 1)) != 0) {
        return WC_EINVAL;
    }
    if (size > SIZE_MAX - align) {
        return WC_EOVERFLOW;
    }
    if (arena->current == NULL) {
        wc_status status = arena_new_block(arena, size + align);
        if (status != WC_OK) {
            return status;
        }
    }

    wc_arena_block *block = arena->current;
    uintptr_t base = (uintptr_t)(block->data + block->used);
    size_t pad = (align - (base & (align - 1))) & (align - 1);
    size_t need = 0;
    if (ckd_add(&need, pad, size)) {
        return WC_EOVERFLOW;
    }
    if (need > block->cap - block->used) {
        wc_status status = arena_new_block(arena, size + align);
        if (status != WC_OK) {
            return status;
        }
        block = arena->current;
        base = (uintptr_t)(block->data + block->used);
        pad = (align - (base & (align - 1))) & (align - 1);
        if (ckd_add(&need, pad, size)) {
            return WC_EOVERFLOW;
        }
        if (need > block->cap) {
            return WC_EOVERFLOW;
        }
    }

    *out = block->data + block->used + pad;
    block->used += need;
    size_t live = 0;
    if (ckd_add(&live, arena->live, need)) {
        return WC_EOVERFLOW;
    }
    arena->live = live;
    if (live > arena->high_water) {
        arena->high_water = live;
    }
    return WC_OK;
}

size_t wc_arena_high_water(const wc_arena *arena) {
    return arena == NULL ? 0 : arena->high_water;
}
