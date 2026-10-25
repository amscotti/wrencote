#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

#include "loop/loop.h"

static void echo(wc_loop *loop, int fd, uint32_t events, void *user) {
    (void)loop;
    (void)events;
    (void)user;
    char byte = 0;
    ssize_t got = read(fd, &byte, 1);
    if (got == 1) {
        ssize_t sent = write(fd, &byte, 1);
        assert(sent == 1);
    }
}

int main(void) {
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    wc_loop *loop = wc_loop_new(NULL);
    assert(loop != NULL);
    assert(wc_loop_add_fd(loop, fds[1], WC_EV_READ, echo, NULL) == WC_OK);
    assert(wc_loop_add_fd(loop, fds[1], WC_EV_READ, echo, NULL) == WC_EBUSY);
    assert(wc_loop_active_fds(loop) >= 2); /* wake fd + echo fd */

    for (int i = 0; i < 100000; i++) {
        char out = (char)('a' + (i % 26));
        assert(write(fds[0], &out, 1) == 1);
        assert(wc_loop_run_once(loop, 100) == WC_OK);
        char in = 0;
        assert(read(fds[0], &in, 1) == 1);
        assert(in == out);
    }

    assert(wc_loop_del_fd(loop, fds[1]) == WC_OK);
    assert(wc_loop_del_fd(loop, fds[1]) == WC_ENOENT);
    assert(wc_loop_mod_fd(loop, fds[1], WC_EV_READ) == WC_ENOENT);
    assert(wc_loop_active_fds(loop) == 1);
    close(fds[0]);
    close(fds[1]);
    wc_loop_free(loop);
    return 0;
}
