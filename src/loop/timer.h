#ifndef WC_LOOP_TIMER_H
#define WC_LOOP_TIMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/compiler.h"
#include "core/status.h"

typedef struct wc_loop wc_loop;
typedef void (*wc_loop_task)(void *user);
typedef struct wc_timer_heap wc_timer_heap;

typedef struct wc_timer {
    int64_t deadline_ns;
    wc_loop_task fn;
    void *user;
    size_t index;
    bool active;
    wc_timer_heap *heap;
} wc_timer;

struct wc_timer_heap {
    wc_timer **items;
    size_t len;
    size_t cap;
};

void wc_timer_heap_init(wc_timer_heap *heap);
void wc_timer_heap_destroy(wc_timer_heap *heap);
WC_NODISCARD wc_status wc_timer_heap_push(wc_timer_heap *heap, wc_timer *timer);
wc_timer *wc_timer_heap_pop(wc_timer_heap *heap);
void wc_timer_heap_remove(wc_timer_heap *heap, wc_timer *timer);
int64_t wc_timer_heap_next_deadline(const wc_timer_heap *heap);

#endif
