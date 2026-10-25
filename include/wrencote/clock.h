#ifndef WRENCOTE_CLOCK_H
#define WRENCOTE_CLOCK_H

#include <stdint.h>

/* Returns monotonic nanoseconds. NULL selects the system clock. */
typedef int64_t (*wc_clock_fn)(void *user);

#endif
