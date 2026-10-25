#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "loop/loop.h"

static int count_fds(void) {
    DIR *dir = opendir("/proc/self/fd");
    assert(dir != NULL);
    int count = 0;
    while (readdir(dir) != NULL) {
        count++;
    }
    closedir(dir);
    return count;
}

static void noop(wc_loop *loop, int fd, uint32_t events, void *user) {
    (void)loop;
    (void)fd;
    (void)events;
    (void)user;
}

int main(void) {
    int fds[2];
    assert(pipe(fds) == 0);
    wc_loop *loop = wc_loop_new(NULL);
    assert(loop != NULL);
    int before = count_fds();
    assert(wc_loop_add_fd(NULL, fds[0], WC_EV_READ, noop, NULL) == WC_EINVAL);
    assert(wc_loop_add_fd(loop, -1, WC_EV_READ, noop, NULL) == WC_EINVAL);
    assert(wc_loop_add_fd(loop, fds[0], WC_EV_READ, NULL, NULL) == WC_EINVAL);
    assert(wc_loop_del_fd(NULL, fds[0]) == WC_EINVAL);
    assert(wc_loop_mod_fd(NULL, fds[0], WC_EV_READ) == WC_EINVAL);
    assert(wc_loop_add_fd(loop, fds[0], 0, noop, NULL) == WC_OK);
    assert(wc_loop_del_fd(loop, fds[0]) == WC_OK);
    for (int i = 0; i < 10000; i++) {
        assert(wc_loop_add_fd(loop, fds[0], WC_EV_READ, noop, NULL) == WC_OK);
        assert(wc_loop_del_fd(loop, fds[0]) == WC_OK);
    }
    assert(wc_loop_active_fds(loop) == 1);
    int after = count_fds();
    assert(before == after);
    close(fds[0]);
    close(fds[1]);
    wc_loop_free(loop);
    wc_loop_free(NULL);
    return 0;
}
