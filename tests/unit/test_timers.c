#include <assert.h>
#include <stdint.h>

#include "loop/loop.h"

typedef struct state {
    int order[8];
    int count;
} state;

static void record_100(void *user) {
    state *s = user;
    s->order[s->count++] = 100;
}

static void record_200(void *user) {
    state *s = user;
    s->order[s->count++] = 200;
}

static void record_300(void *user) {
    state *s = user;
    s->order[s->count++] = 300;
}

static int g_real_fired;

static void moved_timer(void *user) {
    int *flag = user;
    *flag = 1;
}

static void real_timer(void *user) {
    int *flag = user;
    *flag = 1;
}

static int64_t fake_now(void *user) {
    int64_t *value = user;
    return *value;
}

int main(void) {
    int64_t now = 1000;
    wc_clock clock = {fake_now, &now};
    wc_loop *loop = wc_loop_new(&clock);
    assert(loop != NULL);

    state s = {.order = {0}, .count = 0};
    wc_timer *t100 = wc_loop_timer_add(loop, 1100, record_100, &s);
    wc_timer *t200 = wc_loop_timer_add(loop, 1200, record_200, &s);
    wc_timer *t300 = wc_loop_timer_add(loop, 1300, record_300, &s);
    assert(t100 != NULL && t200 != NULL && t300 != NULL);
    wc_timer_cancel(t200);

    assert(wc_loop_run_once(loop, 0) == WC_OK);
    assert(s.count == 0);

    now = 1150;
    assert(wc_loop_run_once(loop, 0) == WC_OK);
    assert(s.count == 1);
    assert(s.order[0] == 100);

    now = 1400;
    assert(wc_loop_run_once(loop, 0) == WC_OK);
    assert(s.count == 2);
    assert(s.order[1] == 300);

    /* A deadline in the past fires on the next turn. */
    wc_timer *past = wc_loop_timer_add(loop, 1, record_200, &s);
    assert(past != NULL);
    assert(wc_loop_run_once(loop, 0) == WC_OK);
    assert(s.count == 3 && s.order[2] == 200);

    wc_timer_cancel(NULL);
    wc_timer *orphan = wc_loop_timer_add(loop, 5000, record_100, &s);
    assert(orphan != NULL);
    wc_timer_cancel(orphan);
    assert(wc_loop_run_once(loop, 0) == WC_OK);
    assert(s.count == 3);

    wc_loop_free(loop);
    assert(wc_loop_timer_add(NULL, 1, record_100, &s) == NULL);

    int64_t moved_now = 5000;
    wc_clock moved_clock = {fake_now, &moved_now};
    wc_loop *moved_loop = wc_loop_new(&moved_clock);
    assert(moved_loop != NULL);
    int moved_fired = 0;
    wc_timer *moved = wc_loop_timer_add(moved_loop, 9000, moved_timer, &moved_fired);
    assert(moved != NULL);
    assert(wc_timer_reschedule(moved, 6000) == WC_OK);
    assert(wc_loop_run_once(moved_loop, 0) == WC_OK);
    assert(moved_fired == 0);
    moved_now = 6001;
    assert(wc_loop_run_once(moved_loop, 0) == WC_OK);
    assert(moved_fired == 1);
    wc_loop_free(moved_loop);

    wc_loop *real = wc_loop_new(NULL);
    assert(real != NULL);
    int64_t start = wc_clock_now_ns(NULL);
    int64_t deadline = start + 10 * 1000 * 1000LL;
    assert(wc_loop_timer_add(real, deadline, real_timer, &g_real_fired) != NULL);
    assert(wc_loop_run_once(real, -1) == WC_OK);
    assert(g_real_fired == 1);
    int64_t elapsed = wc_clock_now_ns(NULL) - start;
    assert(elapsed >= 10 * 1000 * 1000LL);
    assert(elapsed < 100 * 1000 * 1000LL);
    wc_loop_free(real);
    return 0;
}
