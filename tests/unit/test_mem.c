#include <assert.h>
#include <stdint.h>

#include "core/mem.h"

int main(void) {
    assert(wc_mem_malloc(0) == NULL);
    assert(wc_mem_calloc(0, 4) == NULL);
    assert(wc_mem_calloc(SIZE_MAX, 2) == NULL);
    assert(wc_mem_realloc(NULL, 0) == NULL);
    wc_mem_free(NULL);

    void *pointer = wc_mem_malloc(16);
    assert(pointer != NULL);
    pointer = wc_mem_realloc(pointer, 32);
    assert(pointer != NULL);
    wc_mem_free(pointer);

    void *zeroed = wc_mem_calloc(2, 8);
    assert(zeroed != NULL);
    const unsigned char *bytes = zeroed;
    for (size_t i = 0; i < 16; i++) {
        assert(bytes[i] == 0);
    }
    wc_mem_free(zeroed);

    wc_mem_reset_stats();
    assert(wc_mem_alloc_count() == 0);
    pointer = wc_mem_malloc(8);
    assert(pointer != NULL);
    assert(wc_mem_alloc_count() == 1);
    pointer = wc_mem_realloc(pointer, 16);
    assert(pointer != NULL);
    assert(wc_mem_alloc_count() == 2);
    wc_mem_free(pointer);
    return 0;
}
