#ifndef WC_LOOP_LOOP_H
#define WC_LOOP_LOOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/clock.h"
#include "core/status.h"
#include "loop/poll.h"
#include "loop/timer.h"

typedef struct wc_loop wc_loop;

/* Called with the events that are ready for [fd]. */
typedef void (*wc_loop_fd_fn)(wc_loop *loop, int fd, uint32_t events, void *user);

wc_loop *wc_loop_new(const wc_clock *clock);
void wc_loop_free(wc_loop *loop);

WC_NODISCARD wc_status wc_loop_add_fd(wc_loop *loop, int fd, uint32_t events, wc_loop_fd_fn fn,
                                      void *user);
WC_NODISCARD wc_status wc_loop_del_fd(wc_loop *loop, int fd);
WC_NODISCARD wc_status wc_loop_mod_fd(wc_loop *loop, int fd, uint32_t events);

/* Queues a task for the next loop turn. Not thread-safe; call from the loop
 * thread. [wc_loop_stop] is the thread-safe/async-signal-safe operation. */
WC_NODISCARD wc_status wc_loop_defer(wc_loop *loop, wc_loop_task task, void *user);

WC_NODISCARD wc_status wc_loop_run(wc_loop *loop);
WC_NODISCARD wc_status wc_loop_run_once(wc_loop *loop, int timeout_ms);
void wc_loop_stop(wc_loop *loop);
bool wc_loop_stopped(const wc_loop *loop);
size_t wc_loop_active_fds(const wc_loop *loop);
int64_t wc_loop_now_ns(const wc_loop *loop);

/* Adds a timer owned by the loop; free with wc_timer_cancel (or loop free). */
wc_timer *wc_loop_timer_add(wc_loop *loop, int64_t deadline_ns, wc_loop_task fn, void *user);
void wc_timer_cancel(wc_timer *timer);
WC_NODISCARD wc_status wc_timer_reschedule(wc_timer *timer, int64_t deadline_ns);

/* Blocks [signum] and stops the loop when it arrives. */
WC_NODISCARD wc_status wc_loop_watch_signal(wc_loop *loop, int signum);

#endif
