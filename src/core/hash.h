#ifndef WC_CORE_HASH_H
#define WC_CORE_HASH_H

#include <stddef.h>
#include <stdint.h>

#include "core/str.h"

/* FNV-1a 64-bit. Stable across runs; used for header lookup. */
uint64_t wc_hash_fnv1a(const void *data, size_t len);
uint64_t wc_hash_fnv1a_str(wc_str value);

#endif
