#include "core/mem.h"

#include <stdlib.h>

#include "core/checked.h"

static size_t g_alloc_count = 0;

void *wc_mem_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    void *pointer = malloc(size);
    if (pointer != NULL) {
        g_alloc_count++;
    }
    return pointer;
}

void *wc_mem_calloc(size_t count, size_t size) {
    size_t total = 0;
    if (ckd_mul(&total, count, size) || total == 0) {
        return NULL;
    }
    void *pointer = calloc(count, size);
    if (pointer != NULL) {
        g_alloc_count++;
    }
    return pointer;
}

void *wc_mem_realloc(void *pointer, size_t size) {
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    void *result = realloc(pointer, size);
    if (result != NULL) {
        g_alloc_count++;
    }
    return result;
}

void wc_mem_free(void *pointer) {
    free(pointer);
}

size_t wc_mem_alloc_count(void) {
    return g_alloc_count;
}

void wc_mem_reset_stats(void) {
    g_alloc_count = 0;
}
