#include "loop/transport.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/mem.h"
#include "tls/tls.h"

typedef struct tls_transport_state {
    wc_tls_conn *conn;
    bool handshaken;
} tls_transport_state;

static int map_read(wc_tls_result result, size_t *amount) {
    if (result == WC_TLS_OK) {
        return 0;
    }
    if (result == WC_TLS_CLOSED) {
        errno = 0;
        return 0;
    }
    errno = result == WC_TLS_WANT_READ || result == WC_TLS_WANT_WRITE ? EAGAIN : EIO;
    (void)amount;
    return -1;
}

static ssize_t tls_read(wc_transport *transport, void *buffer, size_t len) {
    tls_transport_state *state = transport->state;
    if (!state->handshaken) {
        wc_tls_result result = wc_tls_handshake(state->conn);
        if (result == WC_TLS_WANT_READ || result == WC_TLS_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        if (result != WC_TLS_OK) {
            errno = EIO;
            return -1;
        }
        state->handshaken = true;
    }
    size_t amount = 0;
    wc_tls_result result = wc_tls_read(state->conn, buffer, len, &amount);
    if (result == WC_TLS_OK) {
        return (ssize_t)amount;
    }
    return map_read(result, &amount) < 0 ? -1 : 0;
}

static ssize_t tls_writev(wc_transport *transport, const struct iovec *iov, int iov_count) {
    tls_transport_state *state = transport->state;
    if (!state->handshaken) {
        errno = EAGAIN;
        return -1;
    }
    size_t total = 0;
    for (int i = 0; i < iov_count; i++) {
        size_t amount = 0;
        wc_tls_result result = wc_tls_write(state->conn, iov[i].iov_base, iov[i].iov_len, &amount);
        total += amount;
        if (result == WC_TLS_OK) {
            continue;
        }
        if ((result == WC_TLS_WANT_READ || result == WC_TLS_WANT_WRITE) && total > 0) {
            return (ssize_t)total;
        }
        if (result == WC_TLS_WANT_READ || result == WC_TLS_WANT_WRITE) {
            errno = EAGAIN;
            return -1;
        }
        errno = EIO;
        return total > 0 ? (ssize_t)total : -1;
    }
    return (ssize_t)total;
}

static wc_status tls_flush(wc_transport *transport) {
    (void)transport;
    return WC_OK;
}

static void tls_close(wc_transport *transport) {
    tls_transport_state *state = transport->state;
    if (state != NULL) {
        wc_tls_conn_free(state->conn);
        wc_mem_free(state);
        transport->state = NULL;
    }
    if (transport->fd >= 0) {
        close(transport->fd);
        transport->fd = -1;
    }
}

static const char *tls_alpn(wc_transport *transport) {
    tls_transport_state *state = transport->state;
    return state != NULL ? wc_tls_alpn(state->conn) : NULL;
}

static const wc_transport_ops TLS_OPS = {
    tls_read, tls_writev, tls_flush, tls_alpn, tls_close,
};

wc_transport *wc_tls_transport_new(void *context, int fd) {
    if (context == NULL || fd < 0) {
        return NULL;
    }
    wc_transport *transport = wc_mem_malloc(sizeof *transport);
    if (transport == NULL) {
        return NULL;
    }
    tls_transport_state *state = wc_mem_calloc(1, sizeof *state);
    if (state == NULL) {
        wc_mem_free(transport);
        return NULL;
    }
    if (wc_tls_conn_new(context, fd, &state->conn) != WC_OK) {
        wc_mem_free(state);
        wc_mem_free(transport);
        return NULL;
    }
    transport->ops = &TLS_OPS;
    transport->fd = fd;
    transport->state = state;
    return transport;
}
