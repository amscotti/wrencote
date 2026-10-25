#include "loop/transport.h"

#include <unistd.h>

#include "core/mem.h"

static ssize_t plain_read(wc_transport *transport, void *buffer, size_t len) {
    return read(transport->fd, buffer, len);
}

static ssize_t plain_writev(wc_transport *transport, const struct iovec *iov, int iov_count) {
    return writev(transport->fd, iov, iov_count);
}

static wc_status plain_flush(wc_transport *transport) {
    (void)transport;
    return WC_OK;
}

static void plain_close(wc_transport *transport) {
    if (transport->fd >= 0) {
        close(transport->fd);
        transport->fd = -1;
    }
}

static const char *plain_alpn(wc_transport *transport) {
    (void)transport;
    return NULL;
}

static const wc_transport_ops PLAIN_OPS = {
    plain_read, plain_writev, plain_flush, plain_alpn, NULL, plain_close,
};
wc_transport *wc_transport_plain_new(int fd) {
    if (fd < 0) {
        return NULL;
    }
    wc_transport *transport = wc_mem_malloc(sizeof *transport);
    if (transport == NULL) {
        return NULL;
    }
    transport->ops = &PLAIN_OPS;
    transport->fd = fd;
    transport->state = NULL;
    return transport;
}

void wc_transport_free(wc_transport *transport) {
    if (transport == NULL) {
        return;
    }
    if (transport->ops != NULL && transport->ops->close != NULL) {
        transport->ops->close(transport);
    }
    wc_mem_free(transport);
}

ssize_t wc_transport_read(wc_transport *transport, void *buffer, size_t len) {
    if (transport == NULL || transport->ops == NULL || transport->ops->read == NULL) {
        return -1;
    }
    return transport->ops->read(transport, buffer, len);
}

ssize_t wc_transport_writev(wc_transport *transport, const struct iovec *iov, int iov_count) {
    if (transport == NULL || transport->ops == NULL || transport->ops->writev == NULL) {
        return -1;
    }
    return transport->ops->writev(transport, iov, iov_count);
}

const char *wc_transport_alpn(wc_transport *transport) {
    if (transport == NULL || transport->ops == NULL || transport->ops->alpn == NULL) {
        return NULL;
    }
    return transport->ops->alpn(transport);
}

uint32_t wc_transport_blocked_on(wc_transport *transport) {
    if (transport == NULL || transport->ops == NULL || transport->ops->blocked_on == NULL) {
        return 0;
    }
    return transport->ops->blocked_on(transport);
}

wc_status wc_transport_flush(wc_transport *transport) {
    if (transport == NULL || transport->ops == NULL || transport->ops->flush == NULL) {
        return WC_EINVAL;
    }
    return transport->ops->flush(transport);
}

void wc_transport_close(wc_transport *transport) {
    if (transport == NULL || transport->ops == NULL || transport->ops->close == NULL) {
        return;
    }
    transport->ops->close(transport);
}
