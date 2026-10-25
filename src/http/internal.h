#ifndef WC_HTTP_INTERNAL_H
#define WC_HTTP_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/arena.h"
#include "core/buf.h"
#include "http/http.h"
#include "http/response.h"
#include "http/server.h"
#include "http2/server.h"
#include "loop/timer.h"
#include "loop/transport.h"

struct wc_http_response {
    wc_http_conn *conn;
    wc_h2_server *h2;
    bool h2_dispatching;
    int32_t h2_stream;
    wc_buf headers;
    size_t header_count;
    bool sent;
    bool streaming;
    bool has_content_type;
};

struct wc_http_conn {
    wc_http_server *server;
    wc_http_conn *next;
    wc_http_conn *prev;
    int fd;
    wc_transport *transport;
    wc_h2_server *h2;
    bool h2_dispatching;
    char *head;
    size_t head_len;
    size_t head_cap;
    size_t head_scan;
    wc_arena arena;
    char *body;
    size_t body_need;
    size_t body_have;
    wc_http_request request;
    wc_http_response response;
    wc_buf out;
    size_t out_sent;
    wc_timer *timer;
    size_t requests;
    size_t consume_after;
    bool have_request;
    bool dispatched;
    bool closing;
    /* Set by wc_http_conn_close; the struct itself is freed later (reap) so
     * callbacks deeper in the call stack can still test liveness. */
    bool closed;
};

struct wc_http_server {
    wc_loop *loop;
    wc_http_server_config config;
    wc_http_handler handler;
    void *user;
    int listen_fd;
    uint16_t port;
    wc_h2_request_cb h2_handler;
    void *h2_user;
    wc_http_conn *conns;
    /* Connections closed during the current event batch; freed by reap_dead. */
    wc_http_conn *dead;
    size_t conn_count;
    /* One-shot timer that re-arms the listener after fd exhaustion. */
    wc_timer *accept_pause;
};

bool wc_http_conn_flush(wc_http_conn *conn);
void wc_http_conn_extend_timeout(wc_http_conn *conn, int timeout_ms);
void wc_http_conn_close(wc_http_conn *conn);

#endif
