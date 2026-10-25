#include <stddef.h>
#include <stdlib.h>

#include "vm/vm.h"

typedef union wc_alloc_header {
    size_t size;
    max_align_t align;
} wc_alloc_header;

void *wc_vm_realloc(void *memory, size_t new_size, void *user_data) {
    wc_vm *vm = user_data;
    if (vm == NULL) {
        return NULL;
    }
    if (new_size == 0) {
        if (memory != NULL) {
            wc_alloc_header *header = (wc_alloc_header *)memory - 1;
            if (vm->heap_used >= header->size) {
                vm->heap_used -= header->size;
            }
            free(header);
        }
        return NULL;
    }

    size_t old_size = 0;
    wc_alloc_header *header = NULL;
    if (memory != NULL) {
        header = (wc_alloc_header *)memory - 1;
        old_size = header->size;
    }
    size_t base = vm->heap_used >= old_size ? vm->heap_used - old_size : 0;
    /* Wren does not propagate allocator failure: returning NULL here corrupts
     * the VM. The budget is advisory; the hard limit is process-level. */
    if (new_size > vm->heap_limit || base > vm->heap_limit - new_size) {
        vm->over_budget = true;
    }

    wc_alloc_header *grown = realloc(header, sizeof(wc_alloc_header) + new_size);
    if (grown == NULL) {
        vm->oom = true;
        return NULL;
    }
    grown->size = new_size;
    vm->heap_used = base + new_size;
    if (vm->heap_used > vm->heap_peak) {
        vm->heap_peak = vm->heap_used;
    }
    return grown + 1;
}
