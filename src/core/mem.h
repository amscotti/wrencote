#ifndef WC_CORE_MEM_H
#define WC_CORE_MEM_H

#include <stddef.h>

/* Host allocation wrappers. Single-threaded by design; counters are for tests. */
void *wc_mem_malloc(size_t size);
void *wc_mem_calloc(size_t count, size_t size);
void *wc_mem_realloc(void *pointer, size_t size);
void wc_mem_free(void *pointer);

/* Number of allocation calls since the last reset (realloc counts as one). */
size_t wc_mem_alloc_count(void);
void wc_mem_reset_stats(void);

#endif
