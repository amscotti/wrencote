#include <assert.h>
#include <stdint.h>

#include "core/arena.h"

int main(void) {
    wc_arena arena;
    wc_arena_init(&arena, 64);

    void *first = NULL;
    assert(wc_arena_alloc(&arena, 16, 0, &first) == WC_OK);
    assert(first != NULL);
    assert(((uintptr_t)first % _Alignof(max_align_t)) == 0);

    void *aligned = NULL;
    assert(wc_arena_alloc(&arena, 16, 32, &aligned) == WC_OK);
    assert(((uintptr_t)aligned % 32) == 0);

    void *nothing = (void *)1;
    assert(wc_arena_alloc(&arena, 0, 0, &nothing) == WC_OK);
    assert(nothing == NULL);

    void *bad = NULL;
    assert(wc_arena_alloc(&arena, 8, 3, &bad) == WC_EINVAL);
    assert(wc_arena_alloc(&arena, SIZE_MAX, 0, &bad) == WC_EOVERFLOW);
    assert(wc_arena_alloc(NULL, 8, 0, &bad) == WC_EINVAL);

    size_t high_water = wc_arena_high_water(&arena);
    assert(high_water >= 32);

    wc_arena_reset(&arena);
    void *again = NULL;
    assert(wc_arena_alloc(&arena, 16, 0, &again) == WC_OK);
    assert(again == first);

    /* A new block is created for an allocation larger than the block size. */
    void *large = NULL;
    assert(wc_arena_alloc(&arena, 1024, 0, &large) == WC_OK);
    assert(large != NULL);

    wc_arena_destroy(&arena);
    assert(arena.head == NULL);
    assert(arena.current == NULL);

    void *pointer = NULL;
    assert(wc_arena_alloc(NULL, 8, 0, &pointer) == WC_EINVAL);
    assert(wc_arena_alloc(&arena, 8, 0, NULL) == WC_EINVAL);
    assert(wc_arena_high_water(NULL) == 0);
    wc_arena_reset(NULL);
    wc_arena_destroy(NULL);
    wc_arena_init(NULL, 0);
    return 0;
}
