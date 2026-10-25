#ifndef WC_CORE_CLOCK_H
#define WC_CORE_CLOCK_H

#include <stdint.h>

#include "wrencote/clock.h"

typedef struct wc_clock {
    wc_clock_fn fn;
    void *user;
} wc_clock;

/* Monotonic nanoseconds since an unspecified epoch. */
int64_t wc_clock_now_ns(const wc_clock *clock);

/* The default monotonic system clock. */
wc_clock wc_clock_monotonic(void);

#endif
