#include <assert.h>
#include <signal.h>
#include <unistd.h>

#include "loop/loop.h"

int main(void) {
    wc_loop *loop = wc_loop_new(NULL);
    assert(loop != NULL);
    assert(wc_loop_watch_signal(loop, SIGUSR1) == WC_OK);
    assert(wc_loop_watch_signal(loop, SIGUSR1) == WC_OK);
    assert(wc_loop_watch_signal(loop, -1) == WC_EINVAL);
    assert(raise(SIGUSR1) == 0);
    assert(wc_loop_run_once(loop, 1000) == WC_OK);
    assert(wc_loop_stopped(loop));
    wc_loop_free(loop);
    return 0;
}
