#include <assert.h>

#include "core/str.h"

int main(void) {
    wc_str hello = wc_str_from_cstr("hello");
    wc_str hello2 = wc_str_from_parts("hello", 5);
    wc_str upper = wc_str_from_cstr("HeLLo");
    wc_str hell = wc_str_from_cstr("hell");
    wc_str lo = wc_str_from_cstr("lo");
    wc_str empty = wc_str_from_cstr("");
    wc_str none = wc_str_from_parts(NULL, 0);

    assert(hello.len == 5);
    assert(!wc_str_empty(hello));
    assert(wc_str_empty(empty));
    assert(wc_str_empty(none));
    assert(wc_str_eq(hello, hello2));
    assert(!wc_str_eq(hello, upper));
    assert(wc_str_eq_ci(hello, upper));
    assert(!wc_str_eq_ci(hello, hell));
    assert(wc_str_starts_with(hello, hell));
    assert(!wc_str_starts_with(hell, hello));
    assert(wc_str_ends_with(hello, lo));
    assert(!wc_str_ends_with(hello, hell));
    assert(wc_str_starts_with(hello, empty));
    assert(wc_str_ends_with(hello, empty));
    assert(!wc_str_eq(hello, empty));
    assert(wc_str_eq(empty, none));

    wc_str broken = wc_str_from_parts(NULL, 3);
    assert(!wc_str_eq(broken, broken));
    assert(!wc_str_eq_ci(broken, broken));
    assert(!wc_str_starts_with(hello, broken));
    assert(!wc_str_ends_with(hello, broken));
    assert(!wc_str_eq(hello, wc_str_from_parts(NULL, 5)));
    return 0;
}
