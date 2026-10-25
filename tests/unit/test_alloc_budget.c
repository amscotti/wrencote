#include <assert.h>
#include <stddef.h>

#include "core/arena.h"
#include "core/buf.h"
#include "core/mem.h"

int main(void) {
    wc_arena arena;
    wc_arena_init(&arena, 4096);

    /* Warm the arena: first pass allocates blocks. */
    for (int i = 0; i < 100; i++) {
        void *pointer = NULL;
        assert(wc_arena_alloc(&arena, 32, 0, &pointer) == WC_OK);
        assert(pointer != NULL);
    }
    wc_arena_reset(&arena);

    wc_mem_reset_stats();
    for (int i = 0; i < 100; i++) {
        void *pointer = NULL;
        assert(wc_arena_alloc(&arena, 32, 0, &pointer) == WC_OK);
        assert(pointer != NULL);
    }
    assert(wc_mem_alloc_count() == 0);

    wc_buf buffer;
    wc_buf_init(&buffer);
    assert(wc_buf_reserve(&buffer, 1024) == WC_OK);
    assert(wc_buf_append_cstr(&buffer, "warm") == WC_OK);

    wc_mem_reset_stats();
    for (int i = 0; i < 100; i++) {
        assert(wc_buf_append_byte(&buffer, (unsigned char)('a' + (i % 26))) == WC_OK);
    }
    assert(wc_mem_alloc_count() == 0);

    wc_buf_free(&buffer);
    wc_arena_destroy(&arena);
    return 0;
}
