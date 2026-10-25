#ifndef WC_LOOP_POLL_H
#define WC_LOOP_POLL_H

#include <stdint.h>

#include "core/status.h"

#define WC_EV_READ 0x1u
#define WC_EV_WRITE 0x2u
#define WC_EV_ERROR 0x4u

typedef struct wc_poll wc_poll;

typedef struct wc_poll_event {
    int fd;
    uint32_t events;
} wc_poll_event;

wc_poll *wc_poll_create(void);
void wc_poll_destroy(wc_poll *poll);
WC_NODISCARD wc_status wc_poll_add(wc_poll *poll, int fd, uint32_t events, uint32_t token);
WC_NODISCARD wc_status wc_poll_mod(wc_poll *poll, int fd, uint32_t events, uint32_t token);
WC_NODISCARD wc_status wc_poll_del(wc_poll *poll, int fd);
int wc_poll_wait(wc_poll *poll, wc_poll_event *events, int max_events, int timeout_ms);

#endif
