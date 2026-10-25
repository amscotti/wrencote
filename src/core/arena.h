#ifndef WC_CORE_ARENA_H
#define WC_CORE_ARENA_H

#include <stddef.h>

#include "core/compiler.h"
#include "core/status.h"

typedef struct wc_arena_block wc_arena_block;

typedef struct wc_arena {
    wc_arena_block *head;
    wc_arena_block *current;
    size_t block_size;
    size_t live;
    size_t high_water;
} wc_arena;

/* Initializes an empty arena. A zero block size selects the default (64 KiB). */
void wc_arena_init(wc_arena *arena, size_t block_size);

/* Releases every block. The arena must be re-initialized before reuse. */
void wc_arena_destroy(wc_arena *arena);

/* Resets all blocks to empty; allocations are reused, nothing is freed. */
void wc_arena_reset(wc_arena *arena);

/* Allocates [size] bytes aligned to [align] (0 selects max_align_t).
 * Returns WC_EINVAL for a non-power-of-two alignment, WC_EOVERFLOW on
 * arithmetic overflow, WC_ENOMEM when a block cannot be allocated. */
WC_NODISCARD wc_status wc_arena_alloc(wc_arena *arena, size_t size, size_t align, void **out);

/* Maximum bytes handed out since the last reset. */
size_t wc_arena_high_water(const wc_arena *arena);

#endif
