#ifndef WC_VM_STDLIB_H
#define WC_VM_STDLIB_H

#include <stddef.h>

typedef struct wc_stdlib_module {
    const char *name;
    const unsigned char *data;
    size_t len;
} wc_stdlib_module;

extern const wc_stdlib_module wc_stdlib_modules[];
extern const size_t wc_stdlib_module_count;

#endif
