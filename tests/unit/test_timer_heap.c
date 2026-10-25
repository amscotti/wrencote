#include <assert.h>
#include <stdint.h>

#include "loop/timer.h"

static void noop(void *user) {
    (void)user;
}

int main(void) {
    wc_timer_heap heap;
    wc_timer_heap_init(&heap);
    wc_timer_heap_init(NULL);
    assert(wc_timer_heap_next_deadline(&heap) == INT64_MAX);
    assert(wc_timer_heap_next_deadline(NULL) == INT64_MAX);
    assert(wc_timer_heap_pop(&heap) == NULL);
    assert(wc_timer_heap_push(&heap, NULL) == WC_EINVAL);
    assert(wc_timer_heap_push(NULL, NULL) == WC_EINVAL);

    wc_timer first = {0};
    first.deadline_ns = 30;
    first.fn = noop;
    wc_timer second = {0};
    second.deadline_ns = 10;
    second.fn = noop;
    wc_timer third = {0};
    third.deadline_ns = 20;
    third.fn = noop;

    assert(wc_timer_heap_push(&heap, &first) == WC_OK);
    assert(wc_timer_heap_push(&heap, &second) == WC_OK);
    assert(wc_timer_heap_push(&heap, &third) == WC_OK);
    assert(wc_timer_heap_next_deadline(&heap) == 10);

    wc_timer *popped = wc_timer_heap_pop(&heap);
    assert(popped == &second);
    assert(!second.active && second.heap == NULL);

    wc_timer_heap_remove(&heap, &first);
    assert(!first.active);
    wc_timer_heap_remove(&heap, &first);
    wc_timer_heap_remove(&heap, NULL);
    wc_timer_heap_remove(NULL, NULL);

    popped = wc_timer_heap_pop(&heap);
    assert(popped == &third);
    assert(wc_timer_heap_pop(&heap) == NULL);

    wc_timer_heap_destroy(&heap);
    wc_timer_heap_destroy(NULL);
    assert(heap.items == NULL && heap.len == 0 && heap.cap == 0);
    return 0;
}
