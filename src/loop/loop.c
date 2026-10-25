#define _GNU_SOURCE

#include "loop/loop.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "core/mem.h"

#define WC_LOOP_SLOT_MAX 1024
#define WC_LOOP_SIGNAL_MAX 8

typedef struct wc_fd_slot {
    int fd;
    uint32_t events;
    wc_loop_fd_fn fn;
    void *user;
    bool active;
} wc_fd_slot;

typedef struct wc_task_entry {
    wc_loop_task fn;
    void *user;
} wc_task_entry;

struct wc_loop {
    wc_poll *poll;
    int wake_fd;
    int signal_fd;
    wc_clock clock;
    atomic_bool stopped;
    wc_fd_slot slots[WC_LOOP_SLOT_MAX];
    size_t slot_count;
    wc_timer_heap timers;
    wc_task_entry *tasks;
    size_t task_len;
    size_t task_cap;
    size_t task_head;
    int signals[WC_LOOP_SIGNAL_MAX];
    size_t signal_count;
};

static int timer_timeout_ms(wc_loop *loop, int requested_ms) {
    if (requested_ms >= 0) {
        return requested_ms;
    }
    int64_t next = wc_timer_heap_next_deadline(&loop->timers);
    if (next == INT64_MAX) {
        return -1;
    }
    int64_t now = wc_clock_now_ns(&loop->clock);
    int64_t delta = next - now;
    if (delta <= 0) {
        return 0;
    }
    int64_t ms = (delta + 999999) / 1000000;
    return ms > INT_MAX ? INT_MAX : (int)ms;
}

static void wake_callback(wc_loop *loop, int fd, uint32_t events, void *user) {
    (void)events;
    (void)user;
    uint64_t value = 0;
    while (read(fd, &value, sizeof value) == (ssize_t)sizeof value) {
    }
    (void)loop;
}

static void signal_callback(wc_loop *loop, int fd, uint32_t events, void *user) {
    (void)events;
    (void)user;
    struct signalfd_siginfo info;
    while (read(fd, &info, sizeof info) == (ssize_t)sizeof info) {
    }
    wc_loop_stop(loop);
}

static int find_slot(wc_loop *loop, int fd) {
    for (size_t i = 0; i < loop->slot_count; i++) {
        if (loop->slots[i].active && loop->slots[i].fd == fd) {
            return (int)i;
        }
    }
    return -1;
}

wc_loop *wc_loop_new(const wc_clock *clock) {
    wc_loop *loop = wc_mem_calloc(1, sizeof *loop);
    if (loop == NULL) {
        return NULL;
    }
    loop->poll = wc_poll_create();
    if (loop->poll == NULL) {
        wc_mem_free(loop);
        return NULL;
    }
    loop->wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (loop->wake_fd < 0) {
        wc_poll_destroy(loop->poll);
        wc_mem_free(loop);
        return NULL;
    }
    loop->signal_fd = -1;
    loop->clock = clock == NULL ? wc_clock_monotonic() : *clock;
    atomic_init(&loop->stopped, false);
    wc_timer_heap_init(&loop->timers);
    if (wc_loop_add_fd(loop, loop->wake_fd, WC_EV_READ, wake_callback, loop) != WC_OK) {
        close(loop->wake_fd);
        wc_poll_destroy(loop->poll);
        wc_mem_free(loop);
        return NULL;
    }
    return loop;
}

void wc_loop_free(wc_loop *loop) {
    if (loop == NULL) {
        return;
    }
    while (loop->timers.len > 0) {
        wc_timer *timer = wc_timer_heap_pop(&loop->timers);
        wc_mem_free(timer);
    }
    wc_timer_heap_destroy(&loop->timers);
    wc_mem_free(loop->tasks);
    for (size_t i = 0; i < loop->signal_count; i++) {
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, loop->signals[i]);
        sigprocmask(SIG_UNBLOCK, &set, NULL);
    }
    if (loop->signal_fd >= 0) {
        close(loop->signal_fd);
    }
    close(loop->wake_fd);
    wc_poll_destroy(loop->poll);
    wc_mem_free(loop);
}

WC_NODISCARD wc_status wc_loop_add_fd(wc_loop *loop, int fd, uint32_t events, wc_loop_fd_fn fn,
                                      void *user) {
    if (loop == NULL || fd < 0 || fn == NULL) {
        return WC_EINVAL;
    }
    if (find_slot(loop, fd) >= 0) {
        return WC_EBUSY;
    }
    size_t index = loop->slot_count;
    for (size_t i = 0; i < loop->slot_count; i++) {
        if (!loop->slots[i].active) {
            index = i;
            break;
        }
    }
    if (index == loop->slot_count) {
        if (loop->slot_count >= WC_LOOP_SLOT_MAX) {
            return WC_ENOSPC;
        }
        loop->slot_count++;
    }
    wc_status status = wc_poll_add(loop->poll, fd, events, (uint32_t)index);
    if (status != WC_OK) {
        return status;
    }
    loop->slots[index].fd = fd;
    loop->slots[index].events = events;
    loop->slots[index].fn = fn;
    loop->slots[index].user = user;
    loop->slots[index].active = true;
    return WC_OK;
}

WC_NODISCARD wc_status wc_loop_del_fd(wc_loop *loop, int fd) {
    if (loop == NULL) {
        return WC_EINVAL;
    }
    int index = find_slot(loop, fd);
    if (index < 0) {
        return WC_ENOENT;
    }
    wc_status status = wc_poll_del(loop->poll, fd);
    loop->slots[index].active = false;
    return status;
}

WC_NODISCARD wc_status wc_loop_mod_fd(wc_loop *loop, int fd, uint32_t events) {
    if (loop == NULL) {
        return WC_EINVAL;
    }
    int index = find_slot(loop, fd);
    if (index < 0) {
        return WC_ENOENT;
    }
    wc_status status = wc_poll_mod(loop->poll, fd, events, (uint32_t)index);
    if (status == WC_OK) {
        loop->slots[index].events = events;
    }
    return status;
}

WC_NODISCARD wc_status wc_loop_defer(wc_loop *loop, wc_loop_task task, void *user) {
    if (loop == NULL || task == NULL) {
        return WC_EINVAL;
    }
    if (loop->task_len == loop->task_cap) {
        size_t cap = loop->task_cap == 0 ? 16 : loop->task_cap * 2;
        wc_task_entry *tasks = wc_mem_realloc(loop->tasks, cap * sizeof *tasks);
        if (tasks == NULL) {
            return WC_ENOMEM;
        }
        loop->tasks = tasks;
        loop->task_cap = cap;
    }
    loop->tasks[loop->task_len].fn = task;
    loop->tasks[loop->task_len].user = user;
    loop->task_len++;
    uint64_t value = 1;
    if (write(loop->wake_fd, &value, sizeof value) < 0) {
        return WC_EIO;
    }
    return WC_OK;
}

static void drain_tasks(wc_loop *loop) {
    while (loop->task_head < loop->task_len) {
        wc_task_entry entry = loop->tasks[loop->task_head++];
        entry.fn(entry.user);
    }
    loop->task_head = 0;
    loop->task_len = 0;
}

static void run_due_timers(wc_loop *loop) {
    int64_t now = wc_clock_now_ns(&loop->clock);
    while (loop->timers.len > 0) {
        wc_timer *timer = loop->timers.items[0];
        if (timer->deadline_ns > now) {
            break;
        }
        timer = wc_timer_heap_pop(&loop->timers);
        wc_loop_task fn = timer->fn;
        void *user = timer->user;
        wc_mem_free(timer);
        fn(user);
    }
}

WC_NODISCARD wc_status wc_loop_run_once(wc_loop *loop, int timeout_ms) {
    if (loop == NULL) {
        return WC_EINVAL;
    }
    wc_poll_event events[64];
    int timeout = timer_timeout_ms(loop, timeout_ms);
    int count = wc_poll_wait(loop->poll, events, 64, timeout);
    if (count < 0) {
        return WC_EIO;
    }
    for (int i = 0; i < count; i++) {
        size_t index = (size_t)events[i].fd;
        if (index >= loop->slot_count || !loop->slots[index].active) {
            continue;
        }
        wc_fd_slot *slot = &loop->slots[index];
        slot->fn(loop, slot->fd, events[i].events, slot->user);
    }
    run_due_timers(loop);
    drain_tasks(loop);
    return WC_OK;
}

WC_NODISCARD wc_status wc_loop_run(wc_loop *loop) {
    if (loop == NULL) {
        return WC_EINVAL;
    }
    while (!wc_loop_stopped(loop)) {
        wc_status status = wc_loop_run_once(loop, -1);
        if (status != WC_OK) {
            return status;
        }
    }
    return WC_OK;
}

void wc_loop_stop(wc_loop *loop) {
    if (loop == NULL) {
        return;
    }
    atomic_store(&loop->stopped, true);
    uint64_t value = 1;
    ssize_t ignored = write(loop->wake_fd, &value, sizeof value);
    (void)ignored;
}

bool wc_loop_stopped(const wc_loop *loop) {
    return loop == NULL ? true : atomic_load(&loop->stopped);
}

int64_t wc_loop_now_ns(const wc_loop *loop) {
    return loop == NULL ? 0 : wc_clock_now_ns(&loop->clock);
}

size_t wc_loop_active_fds(const wc_loop *loop) {
    if (loop == NULL) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < loop->slot_count; i++) {
        if (loop->slots[i].active) {
            count++;
        }
    }
    return count;
}

wc_timer *wc_loop_timer_add(wc_loop *loop, int64_t deadline_ns, wc_loop_task fn, void *user) {
    if (loop == NULL || fn == NULL) {
        return NULL;
    }
    wc_timer *timer = wc_mem_malloc(sizeof *timer);
    if (timer == NULL) {
        return NULL;
    }
    timer->deadline_ns = deadline_ns;
    timer->fn = fn;
    timer->user = user;
    timer->index = 0;
    timer->active = false;
    timer->heap = NULL;
    if (wc_timer_heap_push(&loop->timers, timer) != WC_OK) {
        wc_mem_free(timer);
        return NULL;
    }
    return timer;
}

wc_status wc_timer_reschedule(wc_timer *timer, int64_t deadline_ns) {
    if (timer == NULL || timer->heap == NULL) {
        return WC_EINVAL;
    }
    wc_timer_heap *heap = timer->heap;
    wc_timer_heap_remove(heap, timer);
    timer->deadline_ns = deadline_ns;
    return wc_timer_heap_push(heap, timer);
}

void wc_timer_cancel(wc_timer *timer) {
    if (timer == NULL) {
        return;
    }
    if (timer->heap != NULL) {
        wc_timer_heap_remove(timer->heap, timer);
    }
    wc_mem_free(timer);
}

WC_NODISCARD wc_status wc_loop_watch_signal(wc_loop *loop, int signum) {
    if (loop == NULL || signum <= 0 || loop->signal_count >= WC_LOOP_SIGNAL_MAX) {
        return WC_EINVAL;
    }
    for (size_t i = 0; i < loop->signal_count; i++) {
        if (loop->signals[i] == signum) {
            return WC_OK;
        }
    }
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, signum);
    if (sigprocmask(SIG_BLOCK, &set, NULL) != 0) {
        return WC_EIO;
    }
    loop->signals[loop->signal_count++] = signum;

    sigset_t mask;
    sigemptyset(&mask);
    for (size_t i = 0; i < loop->signal_count; i++) {
        sigaddset(&mask, loop->signals[i]);
    }
    if (loop->signal_fd < 0) {
        loop->signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
        if (loop->signal_fd < 0) {
            return WC_EIO;
        }
        return wc_loop_add_fd(loop, loop->signal_fd, WC_EV_READ, signal_callback, loop);
    }
    return signalfd(loop->signal_fd, &mask, 0) < 0 ? WC_EIO : WC_OK;
}
