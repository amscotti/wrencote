#include "loop/timer.h"

#include <stdint.h>

#include "core/mem.h"

void wc_timer_heap_init(wc_timer_heap *heap) {
    if (heap == NULL) {
        return;
    }
    heap->items = NULL;
    heap->len = 0;
    heap->cap = 0;
}

void wc_timer_heap_destroy(wc_timer_heap *heap) {
    if (heap == NULL) {
        return;
    }
    wc_mem_free(heap->items);
    wc_timer_heap_init(heap);
}

static void swap(wc_timer_heap *heap, size_t left, size_t right) {
    wc_timer *tmp = heap->items[left];
    heap->items[left] = heap->items[right];
    heap->items[right] = tmp;
    heap->items[left]->index = left;
    heap->items[right]->index = right;
}

static void sift_up(wc_timer_heap *heap, size_t index) {
    while (index > 0) {
        size_t parent = (index - 1) / 2;
        if (heap->items[parent]->deadline_ns <= heap->items[index]->deadline_ns) {
            break;
        }
        swap(heap, parent, index);
        index = parent;
    }
}

static void sift_down(wc_timer_heap *heap, size_t index) {
    for (;;) {
        size_t left = index * 2 + 1;
        size_t right = left + 1;
        size_t smallest = index;
        if (left < heap->len &&
            heap->items[left]->deadline_ns < heap->items[smallest]->deadline_ns) {
            smallest = left;
        }
        if (right < heap->len &&
            heap->items[right]->deadline_ns < heap->items[smallest]->deadline_ns) {
            smallest = right;
        }
        if (smallest == index) {
            break;
        }
        swap(heap, index, smallest);
        index = smallest;
    }
}

WC_NODISCARD wc_status wc_timer_heap_push(wc_timer_heap *heap, wc_timer *timer) {
    if (heap == NULL || timer == NULL) {
        return WC_EINVAL;
    }
    if (heap->len == heap->cap) {
        size_t cap = heap->cap == 0 ? 16 : heap->cap * 2;
        wc_timer **items = wc_mem_realloc(heap->items, cap * sizeof *items);
        if (items == NULL) {
            return WC_ENOMEM;
        }
        heap->items = items;
        heap->cap = cap;
    }
    timer->index = heap->len;
    timer->heap = heap;
    timer->active = true;
    heap->items[heap->len++] = timer;
    sift_up(heap, timer->index);
    return WC_OK;
}

wc_timer *wc_timer_heap_pop(wc_timer_heap *heap) {
    if (heap == NULL || heap->len == 0) {
        return NULL;
    }
    wc_timer *timer = heap->items[0];
    heap->len--;
    if (heap->len > 0) {
        heap->items[0] = heap->items[heap->len];
        heap->items[0]->index = 0;
        sift_down(heap, 0);
    }
    timer->active = false;
    timer->heap = NULL;
    return timer;
}

void wc_timer_heap_remove(wc_timer_heap *heap, wc_timer *timer) {
    if (heap == NULL || timer == NULL || !timer->active || timer->index >= heap->len) {
        return;
    }
    size_t index = timer->index;
    heap->len--;
    if (index != heap->len) {
        heap->items[index] = heap->items[heap->len];
        heap->items[index]->index = index;
        if (index > 0 &&
            heap->items[index]->deadline_ns < heap->items[(index - 1) / 2]->deadline_ns) {
            sift_up(heap, index);
        } else {
            sift_down(heap, index);
        }
    }
    timer->active = false;
    timer->heap = NULL;
}

int64_t wc_timer_heap_next_deadline(const wc_timer_heap *heap) {
    if (heap == NULL || heap->len == 0) {
        return INT64_MAX;
    }
    return heap->items[0]->deadline_ns;
}
