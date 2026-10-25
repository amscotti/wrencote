#include "core/clock.h"

#include <time.h>

static int64_t monotonic_ns(void *user) {
    (void)user;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (int64_t)now.tv_sec * 1000000000LL + (int64_t)now.tv_nsec;
}

int64_t wc_clock_now_ns(const wc_clock *clock) {
    if (clock != NULL && clock->fn != NULL) {
        return clock->fn(clock->user);
    }
    return monotonic_ns(NULL);
}

wc_clock wc_clock_monotonic(void) {
    wc_clock clock = {monotonic_ns, NULL};
    return clock;
}
