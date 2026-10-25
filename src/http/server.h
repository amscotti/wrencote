#ifndef WC_HTTP_SERVER_H
#define WC_HTTP_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/status.h"
#include "http/http.h"
#include "http2/server.h"
#include "loop/loop.h"
#include "loop/transport.h"

typedef struct wc_http_server wc_http_server;
typedef struct wc_http_conn wc_http_conn;
typedef struct wc_http_response wc_http_response;

typedef struct wc_http_server_config {
    size_t max_connections;
    size_t max_head_bytes;
    size_t max_body_bytes;
    size_t max_response_bytes;
    size_t keepalive_requests;
    int idle_timeout_ms;
    int request_timeout_ms;
    wc_transport_factory transport_factory; /* NULL selects plain sockets */
    void *transport_user;
    wc_h2_request_cb h2_handler; /* NULL disables HTTP/2 */
    void *h2_user;
} wc_http_server_config;

/* Called after a complete request is parsed, on the loop thread, with the VM
 * idle. The handler must call wc_http_response_send before returning. */
typedef void (*wc_http_handler)(void *user, wc_http_conn *conn, const wc_http_request *request,
                                wc_http_response *response);

wc_http_server *wc_http_server_new(wc_loop *loop, const wc_http_server_config *config,
                                   wc_http_handler handler, void *user);
void wc_http_server_free(wc_http_server *server);

wc_status wc_http_server_listen(wc_http_server *server, const char *bind_addr, uint16_t port);
uint16_t wc_http_server_port(const wc_http_server *server);
size_t wc_http_server_connections(const wc_http_server *server);

#endif
