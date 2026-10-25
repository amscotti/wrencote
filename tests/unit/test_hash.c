#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "core/hash.h"

int main(void) {
    assert(wc_hash_fnv1a(NULL, 0) == 0xcbf29ce484222325ULL);
    assert(wc_hash_fnv1a("", 0) == 0xcbf29ce484222325ULL);
    assert(wc_hash_fnv1a("a", 1) == 0xaf63dc4c8601ec8cULL);
    assert(wc_hash_fnv1a("foobar", 6) == 0x85944171f73967e8ULL);
    assert(wc_hash_fnv1a_str(wc_str_from_cstr("foobar")) == 0x85944171f73967e8ULL);
    assert(wc_hash_fnv1a("a", 1) != wc_hash_fnv1a("b", 1));
    return 0;
}
