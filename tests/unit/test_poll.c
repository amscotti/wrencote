#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

#include "loop/poll.h"

int main(void) {
    assert(wc_poll_add(NULL, 0, WC_EV_READ, 0) == WC_EINVAL);
    assert(wc_poll_mod(NULL, 0, WC_EV_READ, 0) == WC_EINVAL);
    assert(wc_poll_del(NULL, 0) == WC_EINVAL);
    assert(wc_poll_wait(NULL, NULL, 1, 0) == -1);

    wc_poll *poll = wc_poll_create();
    assert(poll != NULL);
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

    assert(wc_poll_add(poll, -1, WC_EV_READ, 0) == WC_EINVAL);
    assert(wc_poll_add(poll, fds[0], WC_EV_READ, 7) == WC_OK);
    assert(wc_poll_add(poll, fds[0], WC_EV_READ, 7) == WC_EIO);

    wc_poll_event event = {0};
    assert(wc_poll_wait(poll, NULL, 1, 0) == -1);
    assert(wc_poll_wait(poll, &event, 0, 0) == -1);

    assert(write(fds[1], "x", 1) == 1);
    int count = wc_poll_wait(poll, &event, 1, 100);
    assert(count == 1);
    assert(event.token == 7);
    assert((event.events & WC_EV_READ) != 0);

    assert(wc_poll_mod(poll, fds[0], WC_EV_WRITE, 9) == WC_OK);
    count = wc_poll_wait(poll, &event, 1, 100);
    assert(count == 1);
    assert(event.token == 9);
    assert((event.events & WC_EV_WRITE) != 0);

    assert(wc_poll_del(poll, fds[0]) == WC_OK);
    assert(wc_poll_del(poll, fds[0]) == WC_EIO);
    wc_poll_destroy(poll);
    wc_poll_destroy(NULL);
    close(fds[0]);
    close(fds[1]);
    return 0;
}
