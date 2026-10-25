#include "loop/poll.h"

#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "core/mem.h"

struct wc_poll {
    int fd;
};

static uint32_t to_epoll(uint32_t events) {
    uint32_t mask = 0;
    if ((events & WC_EV_READ) != 0) {
        mask |= EPOLLIN;
    }
    if ((events & WC_EV_WRITE) != 0) {
        mask |= EPOLLOUT;
    }
    return mask;
}

wc_poll *wc_poll_create(void) {
    int fd = epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }
    wc_poll *poll = wc_mem_malloc(sizeof *poll);
    if (poll == NULL) {
        close(fd);
        return NULL;
    }
    poll->fd = fd;
    return poll;
}

void wc_poll_destroy(wc_poll *poll) {
    if (poll == NULL) {
        return;
    }
    close(poll->fd);
    wc_mem_free(poll);
}

WC_NODISCARD wc_status wc_poll_add(wc_poll *poll, int fd, uint32_t events, uint32_t token) {
    if (poll == NULL || fd < 0) {
        return WC_EINVAL;
    }
    struct epoll_event event = {0};
    event.events = to_epoll(events);
    event.data.u32 = token;
    if (epoll_ctl(poll->fd, EPOLL_CTL_ADD, fd, &event) != 0) {
        return errno == ENOMEM ? WC_ENOMEM : WC_EIO;
    }
    return WC_OK;
}

WC_NODISCARD wc_status wc_poll_mod(wc_poll *poll, int fd, uint32_t events, uint32_t token) {
    if (poll == NULL || fd < 0) {
        return WC_EINVAL;
    }
    struct epoll_event event = {0};
    event.events = to_epoll(events);
    event.data.u32 = token;
    if (epoll_ctl(poll->fd, EPOLL_CTL_MOD, fd, &event) != 0) {
        return WC_EIO;
    }
    return WC_OK;
}

WC_NODISCARD wc_status wc_poll_del(wc_poll *poll, int fd) {
    if (poll == NULL || fd < 0) {
        return WC_EINVAL;
    }
    if (epoll_ctl(poll->fd, EPOLL_CTL_DEL, fd, NULL) != 0) {
        return WC_EIO;
    }
    return WC_OK;
}

int wc_poll_wait(wc_poll *poll, wc_poll_event *events, int max_events, int timeout_ms) {
    if (poll == NULL || events == NULL || max_events <= 0) {
        return -1;
    }
    struct epoll_event native[64];
    int limit = max_events < 64 ? max_events : 64;
    int count = epoll_wait(poll->fd, native, limit, timeout_ms);
    if (count < 0) {
        return errno == EINTR ? 0 : -1;
    }
    for (int i = 0; i < count; i++) {
        events[i].token = native[i].data.u32;
        events[i].events = 0;
        if ((native[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) != 0) {
            events[i].events |= WC_EV_READ;
        }
        if ((native[i].events & EPOLLOUT) != 0) {
            events[i].events |= WC_EV_WRITE;
        }
        if ((native[i].events & (EPOLLERR | EPOLLHUP)) != 0) {
            events[i].events |= WC_EV_ERROR;
        }
    }
    return count;
}
