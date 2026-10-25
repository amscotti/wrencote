#include <assert.h>
#include <pthread.h>
#include <time.h>

#include "loop/loop.h"

static wc_loop *g_loop;
static int g_ticks;

static void deferred(void *user) {
    (void)user;
    g_ticks++;
}

static void *stopper(void *user) {
    (void)user;
    struct timespec delay = {0, 20 * 1000 * 1000};
    nanosleep(&delay, NULL);
    wc_loop_stop(g_loop);
    return NULL;
}

int main(void) {
    wc_loop *loop = wc_loop_new(NULL);
    assert(loop != NULL);
    g_loop = loop;
    assert(wc_loop_defer(loop, deferred, NULL) == WC_OK);
    assert(wc_loop_defer(NULL, deferred, NULL) == WC_EINVAL);
    assert(wc_loop_defer(loop, NULL, NULL) == WC_EINVAL);

    pthread_t thread;
    assert(pthread_create(&thread, NULL, stopper, NULL) == 0);
    assert(wc_loop_run(loop) == WC_OK);
    assert(pthread_join(thread, NULL) == 0);
    assert(wc_loop_stopped(loop));
    assert(g_ticks == 1);
    wc_loop_free(loop);
    assert(wc_loop_stopped(NULL));
    assert(wc_loop_run(NULL) == WC_EINVAL);
    assert(wc_loop_run_once(NULL, 0) == WC_EINVAL);
    return 0;
}
