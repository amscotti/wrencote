#include "core/hash.h"

#define WC_FNV_OFFSET 14695981039346656037ULL
#define WC_FNV_PRIME 1099511628211ULL

uint64_t wc_hash_fnv1a(const void *data, size_t len) {
    uint64_t hash = WC_FNV_OFFSET;
    if (data == NULL || len == 0) {
        return hash;
    }
    const unsigned char *bytes = data;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= WC_FNV_PRIME;
    }
    return hash;
}

uint64_t wc_hash_fnv1a_str(wc_str value) {
    return wc_hash_fnv1a(value.data, value.len);
}
