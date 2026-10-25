#ifndef WC_LOOP_TRANSPORT_H
#define WC_LOOP_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "core/status.h"
#include "loop/poll.h"

typedef struct wc_transport wc_transport;

typedef struct wc_transport_ops {
    ssize_t (*read)(wc_transport *transport, void *buffer, size_t len);
    ssize_t (*writev)(wc_transport *transport, const struct iovec *iov, int iov_count);
    wc_status (*flush)(wc_transport *transport);
    const char *(*alpn)(wc_transport *transport);
    /* Reports what the transport is currently blocked on (WC_EV_READ /
     * WC_EV_WRITE, 0 when unknown). TLS uses this to request write readiness
     * only when the last SSL operation actually returned WANT_WRITE. */
    uint32_t (*blocked_on)(wc_transport *transport);
    void (*close)(wc_transport *transport);
} wc_transport_ops;

struct wc_transport {
    const wc_transport_ops *ops;
    int fd;
    void *state;
};

/* Creates the byte transport for an accepted socket. NULL means failure. */
typedef wc_transport *(*wc_transport_factory)(void *user, int fd);

wc_transport *wc_transport_plain_new(int fd);
void wc_transport_free(wc_transport *transport);

ssize_t wc_transport_read(wc_transport *transport, void *buffer, size_t len);
ssize_t wc_transport_writev(wc_transport *transport, const struct iovec *iov, int iov_count);
wc_status wc_transport_flush(wc_transport *transport);
const char *wc_transport_alpn(wc_transport *transport);
uint32_t wc_transport_blocked_on(wc_transport *transport);
void wc_transport_close(wc_transport *transport);

#endif
