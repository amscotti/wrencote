#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "core/clock.h"

static int64_t fake_now(void *user) {
    int64_t *value = user;
    return *value;
}

int main(void) {
    int64_t value = 123456789;
    wc_clock clock = {fake_now, &value};
    assert(wc_clock_now_ns(&clock) == value);
    value += 1;
    assert(wc_clock_now_ns(&clock) == value);

    wc_clock empty = {NULL, NULL};
    int64_t system = wc_clock_now_ns(&empty);
    assert(system >= 0);
    assert(wc_clock_now_ns(NULL) >= 0);

    wc_clock monotonic = wc_clock_monotonic();
    assert(monotonic.fn != NULL);
    assert(wc_clock_now_ns(&monotonic) >= 0);
    return 0;
}
