#define _GNU_SOURCE

#include "http/internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "core/mem.h"
#include "http/limits.h"

#define WC_ACCEPT_BURST 32

static void conn_event(wc_loop *loop, int fd, uint32_t events, void *user);
static void listener_event(wc_loop *loop, int fd, uint32_t events, void *user);
static void conn_timer(void *user);
static void conn_process(wc_http_conn *conn);

static void unlink_conn(wc_http_conn *conn) {
    wc_http_server *server = conn->server;
    if (conn->prev != NULL) {
        conn->prev->next = conn->next;
    } else {
        server->conns = conn->next;
    }
    if (conn->next != NULL) {
        conn->next->prev = conn->prev;
    }
    if (server->conn_count > 0) {
        server->conn_count--;
    }
}

void wc_http_conn_close(wc_http_conn *conn) {
    if (conn == NULL || conn->closed) {
        return;
    }
    /* Only the descriptor and scheduling state are torn down here. The
     * buffers and arena stay alive until reap_dead so a handler that closes
     * the connection (e.g. via sseClose) can still read req/res data. */
    conn->closed = true;
    if (conn->fd >= 0) {
        (void)wc_loop_del_fd(conn->server->loop, conn->fd);
        if (conn->transport != NULL) {
            wc_transport_free(conn->transport);
            conn->transport = NULL;
        } else {
            close(conn->fd);
        }
        conn->fd = -1;
    }
    if (conn->timer != NULL) {
        wc_timer_cancel(conn->timer);
        conn->timer = NULL;
    }
    unlink_conn(conn);
    conn->next = conn->server->dead;
    conn->server->dead = conn;
}

/* Releases everything the connection owns. Must only run once no callback can
 * still reference the connection's request/response data. */
static void destroy_conn(wc_http_conn *conn) {
#ifdef WC_ENABLE_HTTP2
    if (conn->h2 != NULL) {
        wc_h2_server_free(conn->h2);
        conn->h2 = NULL;
    }
#endif
    wc_arena_destroy(&conn->arena);
    wc_buf_free(&conn->out);
    wc_buf_free(&conn->response.headers);
    wc_mem_free(conn->head);
    conn->head = NULL;
    wc_mem_free(conn);
}

/* Frees connections closed earlier in this event batch. Deferred freeing lets
 * code deeper in a call stack keep reading conn fields (conn->closed) and
 * request data after a close instead of use-after-freeing them. */
static void reap_dead(wc_http_server *server) {
    while (server->dead != NULL) {
        wc_http_conn *conn = server->dead;
        server->dead = conn->next;
        conn->next = NULL;
        destroy_conn(conn);
    }
}

/* The single place that decides the fd's poll interest: reads always; writes
 * only when output is queued or the transport is waiting on writability, and
 * never while the transport says it must read first. */
static void conn_update_interest(wc_http_conn *conn) {
    if (conn == NULL || conn->closed || conn->fd < 0) {
        return;
    }
    uint32_t blocked = wc_transport_blocked_on(conn->transport);
    bool output_pending = false;
#ifdef WC_ENABLE_HTTP2
    if (conn->h2 != NULL) {
        size_t length = 0;
        (void)wc_h2_server_output(conn->h2, &length);
        output_pending = length > 0;
    } else
#endif
    {
        output_pending = conn->out.len > conn->out_sent;
    }
    uint32_t events = WC_EV_READ;
    if ((blocked & WC_EV_WRITE) != 0) {
        events |= WC_EV_WRITE;
    } else if (output_pending && (blocked & WC_EV_READ) == 0) {
        events |= WC_EV_WRITE;
    }
    (void)wc_loop_mod_fd(conn->server->loop, conn->fd, events);
}

static void set_timer(wc_http_conn *conn, int timeout_ms) {
    if (conn->closed) {
        return;
    }
    int64_t deadline = wc_loop_now_ns(conn->server->loop) + (int64_t)timeout_ms * 1000000;
    if (conn->timer != NULL) {
        (void)wc_timer_reschedule(conn->timer, deadline);
    } else {
        conn->timer = wc_loop_timer_add(conn->server->loop, deadline, conn_timer, conn);
    }
}

void wc_http_conn_extend_timeout(wc_http_conn *conn, int timeout_ms) {
    if (conn != NULL) {
        set_timer(conn, timeout_ms);
    }
}

static void clear_timer(wc_http_conn *conn) {
    if (conn->timer != NULL) {
        wc_timer_cancel(conn->timer);
        conn->timer = NULL;
    }
}

static void send_simple(wc_http_conn *conn, int status) {
    conn->closing = true;
    clear_timer(conn);
    wc_http_response_reset(&conn->response);
    conn->response.conn = conn;
    const char *body = status == 200 ? "ok\n" : "error\n";
    if (wc_http_response_send(&conn->response, status, body, strlen(body)) != WC_OK) {
        wc_http_conn_close(conn);
        return;
    }
    (void)wc_http_conn_flush(conn);
}

static void finish_response(wc_http_conn *conn) {
    if (conn->closed) {
        return;
    }
    if (conn->consume_after > 0) {
        if (conn->head_len >= conn->consume_after) {
            memmove(conn->head, conn->head + conn->consume_after,
                    conn->head_len - conn->consume_after);
            conn->head_len -= conn->consume_after;
        } else {
            conn->head_len = 0;
        }
        conn->consume_after = 0;
    }
    conn->head_scan = 0;
    conn->have_request = false;
    conn->dispatched = false;
    conn->body = NULL;
    conn->body_need = 0;
    conn->body_have = 0;
    wc_arena_reset(&conn->arena);
    wc_http_response_reset(&conn->response);
    set_timer(conn, conn->server->config.idle_timeout_ms);
}

bool wc_http_conn_flush(wc_http_conn *conn) {
    if (conn == NULL || conn->closed) {
        return false;
    }
    while (conn->out_sent < conn->out.len) {
        struct iovec iov = {
            .iov_base = conn->out.data + conn->out_sent,
            .iov_len = conn->out.len - conn->out_sent,
        };
        ssize_t written = wc_transport_writev(conn->transport, &iov, 1);
        if (written > 0) {
            conn->out_sent += (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else if (written < 0 && (errno == EAGAIN)) {
            /* A stalled drain must not outlive a bound: arm a write deadline
             * if the dispatch path cleared the existing timer. */
            if (conn->timer == NULL) {
                set_timer(conn, WC_HTTP_WRITE_TIMEOUT_MS);
            }
            conn_update_interest(conn);
            return true;
        } else {
            if (conn->response.streaming) {
                conn->closing = true;
                return false;
            }
            wc_http_conn_close(conn);
            return false;
        }
    }
    conn->out.len = 0;
    conn->out_sent = 0;
    if (conn->response.streaming) {
        conn_update_interest(conn);
        return true;
    }
    if (conn->closing) {
        wc_http_conn_close(conn);
        return false;
    }
    finish_response(conn);
    conn_update_interest(conn);
    return true;
}

static void dispatch(wc_http_conn *conn) {
    conn->dispatched = true;
    conn->request.body = conn->body;
    conn->request.body_len = conn->body_need;
    conn->requests++;
    bool close_after =
        !conn->request.keep_alive || conn->requests >= conn->server->config.keepalive_requests;
    conn->closing = close_after;
    clear_timer(conn);
    wc_http_response_reset(&conn->response);
    conn->response.conn = conn;
    if (conn->server->handler == NULL) {
        send_simple(conn, 503);
        return;
    }
    conn->server->handler(conn->server->user, conn, &conn->request, &conn->response);
    if (conn->closed) {
        return;
    }
    if (!wc_http_response_sent(&conn->response)) {
        send_simple(conn, 500);
        return;
    }
    (void)wc_http_conn_flush(conn);
}

static void conn_process(wc_http_conn *conn) {
    for (;;) {
        if (conn->fd < 0 || conn->dispatched) {
            return;
        }
        if (conn->out.len > conn->out_sent) {
            (void)wc_http_conn_flush(conn);
            return;
        }
        if (conn->have_request) {
            if (conn->body_have < conn->body_need) {
                return;
            }
            conn->consume_after = conn->request.head_len + conn->body_need;
            dispatch(conn);
            if (conn->closed) {
                return;
            }
            /* The response may have flushed synchronously; keep parsing so
             * pipelined requests already buffered in head are answered. */
            continue;
        }
        if (conn->head_len == 0) {
            return;
        }
        size_t scan = conn->head_scan;
        bool found = false;
        while (scan + 3 < conn->head_len) {
            if (memcmp(conn->head + scan, "\r\n\r\n", 4) == 0) {
                found = true;
                break;
            }
            scan++;
        }
        if (!found) {
            conn->head_scan = conn->head_len > 3 ? conn->head_len - 3 : 0;
            if (conn->head_len >= conn->head_cap) {
                send_simple(conn, 431);
            } else {
                set_timer(conn, conn->server->config.request_timeout_ms);
            }
            return;
        }
        int status = 400;
        wc_http_parse_result parsed =
            wc_http_parse_request_limit(conn->head, conn->head_len, &conn->request, &status,
                                        conn->server->config.max_body_bytes);
        if (parsed != WC_HTTP_PARSE_OK) {
            send_simple(conn, status);
            return;
        }
        conn->have_request = true;
        conn->body_need = conn->request.content_length;
        conn->body_have = 0;
        conn->body = NULL;
        if (conn->body_need > 0) {
            void *body = NULL;
            if (wc_arena_alloc(&conn->arena, conn->body_need, 1, &body) != WC_OK) {
                send_simple(conn, 500);
                return;
            }
            conn->body = body;
            size_t available = conn->head_len - conn->request.head_len;
            size_t copy = available < conn->body_need ? available : conn->body_need;
            memcpy(conn->body, conn->head + conn->request.head_len, copy);
            conn->body_have = copy;
        }
        set_timer(conn, conn->server->config.request_timeout_ms);
    }
}

#ifdef WC_ENABLE_HTTP2
static bool conn_h2_flush(wc_http_conn *conn) {
    for (;;) {
        if (conn->closed) {
            return false;
        }
        size_t length = 0;
        const uint8_t *data = wc_h2_server_output(conn->h2, &length);
        if (length == 0) {
            conn_update_interest(conn);
            return true;
        }
        struct iovec iov = {.iov_base = (void *)(uintptr_t)data, .iov_len = length};
        ssize_t written = wc_transport_writev(conn->transport, &iov, 1);
        if (written > 0) {
            wc_h2_server_output_consume(conn->h2, (size_t)written);
            wc_http_conn_extend_timeout(conn, conn->server->config.idle_timeout_ms);
        } else if (written < 0 && (errno == EAGAIN || errno == EINTR)) {
            if (conn->timer == NULL) {
                set_timer(conn, WC_HTTP_WRITE_TIMEOUT_MS);
            }
            conn_update_interest(conn);
            return true;
        } else {
            if (conn->h2_dispatching) {
                conn->closing = true;
                return false;
            }
            wc_http_conn_close(conn);
            return false;
        }
    }
}

static bool h2_flush_cb(void *user) {
    wc_http_conn *conn = user;
    conn->h2_dispatching = true;
    bool ok = conn_h2_flush(conn);
    conn->h2_dispatching = false;
    return ok;
}

static void conn_h2_read(wc_http_conn *conn) {
    uint8_t buffer[16384];
    /* conn->head doubles as the pending buffer for bytes a feed did not
     * consume. Feed it directly while it fills the read buffer. */
    size_t pending = conn->head_len;
    while (pending >= sizeof buffer) {
        size_t consumed = 0;
        wc_status status =
            wc_h2_server_feed(conn->h2, (const uint8_t *)conn->head, pending, &consumed);
        if (status != WC_OK) {
            if (!conn->closed) {
                wc_http_conn_close(conn);
            }
            return;
        }
        conn->head_len -= consumed;
        pending = conn->head_len;
        if (pending > 0) {
            memmove(conn->head, conn->head + consumed, pending);
        }
        if (consumed == 0) {
            /* No progress is possible without more input; keep the bytes and
             * let the idle timer bound the connection. */
            (void)conn_h2_flush(conn);
            conn_update_interest(conn);
            return;
        }
        wc_http_conn_extend_timeout(conn, conn->server->config.idle_timeout_ms);
    }
    if (pending > 0) {
        memcpy(buffer, conn->head, pending);
    }
    ssize_t got = wc_transport_read(conn->transport, buffer + pending, sizeof buffer - pending);
    /* Keep pending bytes stashed until a read actually delivers data. */
    conn->head_len = pending;
    if (got > 0) {
        size_t total = pending + (size_t)got;
        size_t consumed = 0;
        wc_status status = wc_h2_server_feed(conn->h2, buffer, total, &consumed);
        size_t leftover = total - consumed;
        conn->head_len = 0;
        if (status == WC_OK && leftover > 0) {
            if (leftover > conn->head_cap) {
                status = WC_EOVERFLOW;
            } else {
                memcpy(conn->head, buffer + consumed, leftover);
                conn->head_len = leftover;
            }
        }
        if (status != WC_OK) {
            if (!conn->closed) {
                wc_http_conn_close(conn);
            }
            return;
        }
        if (conn->closed) {
            return;
        }
        /* Any h2 traffic resets the connection idle deadline. */
        wc_http_conn_extend_timeout(conn, conn->server->config.idle_timeout_ms);
        if (!conn_h2_flush(conn)) {
            return;
        }
        if (conn->closed) {
            return;
        }
        if (wc_h2_server_closing(conn->h2)) {
            (void)shutdown(conn->fd, SHUT_WR);
            conn_update_interest(conn);
        }
    } else if (got == 0) {
        /* Peer closed; any unconsumed bytes can no longer be completed. */
        wc_http_conn_close(conn);
    } else if (errno != EAGAIN && errno != EINTR) {
        wc_http_conn_close(conn);
    }
    /* EAGAIN/EINTR: pending bytes (if any) stay stashed for the next event. */
}

static bool conn_switch_to_h2(wc_http_conn *conn) {
    if (conn->h2 != NULL || conn->server->config.h2_handler == NULL) {
        return false;
    }
    const char *alpn = wc_transport_alpn(conn->transport);
    if (alpn == NULL || strcmp(alpn, "h2") != 0) {
        return false;
    }
    conn->h2 = wc_h2_server_new(conn->server->config.h2_handler, conn->server->config.h2_user);
    if (conn->h2 == NULL) {
        wc_http_conn_close(conn);
        return true;
    }
    wc_h2_server_set_limits(conn->h2, conn->server->config.max_body_bytes,
                            conn->server->config.max_response_bytes,
                            conn->server->config.max_head_bytes);
    wc_h2_server_set_flush(conn->h2, h2_flush_cb, conn);
    if (conn->head_len > 0) {
        size_t consumed = 0;
        if (wc_h2_server_feed(conn->h2, (const uint8_t *)conn->head, conn->head_len, &consumed) !=
            WC_OK) {
            if (!conn->closed) {
                wc_http_conn_close(conn);
            }
            return true;
        }
        conn->head_len -= consumed;
        if (conn->head_len > 0) {
            memmove(conn->head, conn->head + consumed, conn->head_len);
        }
    }
    wc_http_conn_extend_timeout(conn, conn->server->config.idle_timeout_ms);
    (void)conn_h2_flush(conn);
    return true;
}
#endif /* WC_ENABLE_HTTP2 */

static void conn_read(wc_http_conn *conn) {
    if (conn->closed) {
        return;
    }
#ifdef WC_ENABLE_HTTP2
    if (conn->h2 != NULL) {
        conn_h2_read(conn);
        return;
    }
#endif
    if (conn->have_request && conn->body_have < conn->body_need) {
        size_t space = conn->body_need - conn->body_have;
        ssize_t got = wc_transport_read(conn->transport, conn->body + conn->body_have, space);
        if (got > 0) {
            conn->body_have += (size_t)got;
            conn_process(conn);
        } else if (got == 0) {
            wc_http_conn_close(conn);
        } else if (errno != EAGAIN && errno != EINTR) {
            wc_http_conn_close(conn);
        }
        return;
    }
    size_t space = conn->head_cap - conn->head_len;
    if (space == 0) {
        /* A response is still flushing and the head buffer is full of
         * pipelined bytes; they are parsed once the response completes. */
        if (conn->dispatched || conn->out.len > conn->out_sent) {
            return;
        }
        send_simple(conn, 431);
        return;
    }
    ssize_t got = wc_transport_read(conn->transport, conn->head + conn->head_len, space);
    if (got > 0) {
        conn->head_len += (size_t)got;
#ifdef WC_ENABLE_HTTP2
        if (conn_switch_to_h2(conn)) {
            return;
        }
#endif
        conn_process(conn);
    } else if (got == 0) {
        wc_http_conn_close(conn);
    } else if (errno != EAGAIN && errno != EINTR) {
        wc_http_conn_close(conn);
    }
}

static void conn_event(wc_loop *loop, int fd, uint32_t events, void *user) {
    (void)loop;
    (void)fd;
    wc_http_conn *conn = user;
    wc_http_server *server = conn->server;
    reap_dead(server);
    if (conn->closed) {
        return;
    }
    if ((events & WC_EV_ERROR) != 0) {
        wc_http_conn_close(conn);
        reap_dead(server);
        return;
    }
    if ((events & WC_EV_WRITE) != 0) {
        bool handled = false;
#ifdef WC_ENABLE_HTTP2
        if (conn->h2 != NULL) {
            (void)conn_h2_flush(conn);
            handled = true;
        }
#endif
        if (!handled && conn->out.len > conn->out_sent) {
            if (wc_http_conn_flush(conn)) {
                conn_process(conn);
            }
            handled = true;
        }
        if (!handled && (wc_transport_blocked_on(conn->transport) & WC_EV_WRITE) != 0) {
            /* The handshake (or a blocked TLS operation) needs writability to
             * make progress; retry it through the read path. */
            conn_read(conn);
        }
        if (conn->closed) {
            reap_dead(server);
            return;
        }
    }
    if ((events & WC_EV_READ) != 0) {
        conn_read(conn);
        if (conn->closed) {
            reap_dead(server);
            return;
        }
    }
    conn_update_interest(conn);
    reap_dead(server);
}

static void conn_timer(void *user) {
    wc_http_conn *conn = user;
    wc_http_server *server = conn->server;
    conn->timer = NULL;
    if (conn->closed) {
        reap_dead(server);
        return;
    }
    /* Never inject a 408 into a response that was already started, and never
     * leave a partially flushed response hanging. */
    if (conn->h2 != NULL || conn->closing || conn->dispatched || conn->response.streaming) {
        wc_http_conn_close(conn);
        reap_dead(server);
        return;
    }
    if (conn->have_request || conn->head_len > 0) {
        send_simple(conn, 408);
    } else {
        wc_http_conn_close(conn);
    }
    reap_dead(server);
}

static wc_http_conn *conn_new(wc_http_server *server, int fd) {
    wc_http_conn *conn = wc_mem_calloc(1, sizeof *conn);
    if (conn == NULL) {
        close(fd);
        return NULL;
    }
    conn->server = server;
    conn->fd = fd;
    conn->transport = server->config.transport_factory != NULL
                          ? server->config.transport_factory(server->config.transport_user, fd)
                          : wc_transport_plain_new(fd);
    if (conn->transport == NULL) {
        close(fd);
        wc_mem_free(conn);
        return NULL;
    }
    conn->head_cap = server->config.max_head_bytes;
    conn->head = wc_mem_malloc(conn->head_cap);
    if (conn->head == NULL) {
        wc_transport_free(conn->transport);
        wc_mem_free(conn);
        return NULL;
    }
    wc_arena_init(&conn->arena, 64 * 1024);
    wc_buf_init(&conn->out);
    wc_buf_init(&conn->response.headers);
    conn->response.conn = conn;
    conn->next = server->conns;
    if (server->conns != NULL) {
        server->conns->prev = conn;
    }
    server->conns = conn;
    server->conn_count++;
    if (wc_loop_add_fd(server->loop, fd, WC_EV_READ, conn_event, conn) != WC_OK) {
        wc_http_conn_close(conn);
        return NULL;
    }
    set_timer(conn, server->config.idle_timeout_ms);
    return conn;
}

/* Re-arms accepting after an EMFILE pause. */
static void listener_resume(void *user) {
    wc_http_server *server = user;
    server->accept_pause = NULL;
    if (server->listen_fd >= 0) {
        (void)wc_loop_mod_fd(server->loop, server->listen_fd, WC_EV_READ);
    }
}

static void listener_event(wc_loop *loop, int fd, uint32_t events, void *user) {
    (void)loop;
    (void)events;
    wc_http_server *server = user;
    reap_dead(server);
    for (int i = 0; i < WC_ACCEPT_BURST; i++) {
        int client = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            if ((errno == EMFILE || errno == ENFILE) && server->accept_pause == NULL) {
                /* Out of fds: stop accepting for a moment instead of spinning
                 * on a level-triggered readable listener. A timer re-arms it. */
                int64_t resume = wc_loop_now_ns(server->loop) + 250 * 1000000LL;
                server->accept_pause =
                    wc_loop_timer_add(server->loop, resume, listener_resume, server);
                if (server->accept_pause != NULL) {
                    (void)wc_loop_mod_fd(server->loop, fd, 0);
                }
            }
            break;
        }
        int one = 1;
        (void)setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        if (server->conn_count >= server->config.max_connections) {
            close(client);
            continue;
        }
        /* conn_new consumes the client fd on every failure path. */
        (void)conn_new(server, client);
    }
    reap_dead(server);
}

wc_http_server *wc_http_server_new(wc_loop *loop, const wc_http_server_config *config,
                                   wc_http_handler handler, void *user) {
    if (loop == NULL || config == NULL) {
        return NULL;
    }
    wc_http_server *server = wc_mem_calloc(1, sizeof *server);
    if (server == NULL) {
        return NULL;
    }
    server->loop = loop;
    server->config = *config;
    if (server->config.max_connections == 0) {
        server->config.max_connections = WC_HTTP_MAX_CONNECTIONS;
    }
    if (server->config.max_head_bytes == 0) {
        server->config.max_head_bytes = WC_HTTP_MAX_HEADER_BYTES;
    }
    if (server->config.max_body_bytes == 0) {
        server->config.max_body_bytes = WC_HTTP_MAX_BODY;
    }
    if (server->config.max_response_bytes == 0) {
        server->config.max_response_bytes = WC_HTTP_MAX_RESPONSE;
    }
    if (server->config.keepalive_requests == 0) {
        server->config.keepalive_requests = WC_HTTP_KEEPALIVE_REQUESTS;
    }
    if (server->config.idle_timeout_ms == 0) {
        server->config.idle_timeout_ms = WC_HTTP_IDLE_TIMEOUT_MS;
    }
    if (server->config.request_timeout_ms == 0) {
        server->config.request_timeout_ms = WC_HTTP_REQUEST_TIMEOUT_MS;
    }
    server->handler = handler;
    server->user = user;
    server->listen_fd = -1;
    return server;
}

wc_status wc_http_server_listen(wc_http_server *server, const char *bind_addr, uint16_t port) {
    if (server == NULL) {
        return WC_EINVAL;
    }
    if (server->listen_fd >= 0) {
        return WC_EBUSY;
    }
    bool ipv6 = bind_addr != NULL && strchr(bind_addr, ':') != NULL;
    struct sockaddr_storage address;
    memset(&address, 0, sizeof address);
    socklen_t address_len;
    if (ipv6) {
        struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)&address;
        v6->sin6_family = AF_INET6;
        v6->sin6_port = htons(port);
        if (inet_pton(AF_INET6, bind_addr, &v6->sin6_addr) != 1) {
            return WC_EINVAL;
        }
        address_len = sizeof *v6;
    } else {
        struct sockaddr_in *v4 = (struct sockaddr_in *)&address;
        v4->sin_family = AF_INET;
        v4->sin_port = htons(port);
        if (bind_addr == NULL) {
            bind_addr = "127.0.0.1";
        }
        if (inet_pton(AF_INET, bind_addr, &v4->sin_addr) != 1) {
            return WC_EINVAL;
        }
        address_len = sizeof *v4;
    }
    int fd = socket(address.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return WC_EIO;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    if (ipv6) {
        /* Dual-stack: one listener serves both IPv4 and IPv6. */
        int dual_stack = 0;
        (void)setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &dual_stack, sizeof dual_stack);
    }
    if (bind(fd, (struct sockaddr *)&address, address_len) != 0) {
        close(fd);
        return WC_EIO;
    }
    if (listen(fd, WC_HTTP_LISTEN_BACKLOG) != 0) {
        close(fd);
        return WC_EIO;
    }
    socklen_t length = sizeof address;
    if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
        close(fd);
        return WC_EIO;
    }
    server->port = ntohs(((struct sockaddr_in *)&address)->sin_port);
    if (wc_loop_add_fd(server->loop, fd, WC_EV_READ, listener_event, server) != WC_OK) {
        close(fd);
        return WC_ENOMEM;
    }
    server->listen_fd = fd;
    return WC_OK;
}

uint16_t wc_http_server_port(const wc_http_server *server) {
    return server == NULL ? 0 : server->port;
}

size_t wc_http_server_connections(const wc_http_server *server) {
    return server == NULL ? 0 : server->conn_count;
}

void wc_http_server_free(wc_http_server *server) {
    if (server == NULL) {
        return;
    }
    /* Not safe to call from inside a request callback: it frees the calling
     * connection (and its request data) while the handler is still running. */
    if (server->accept_pause != NULL) {
        wc_timer_cancel(server->accept_pause);
        server->accept_pause = NULL;
    }
    if (server->listen_fd >= 0) {
        (void)wc_loop_del_fd(server->loop, server->listen_fd);
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    while (server->conns != NULL) {
        wc_http_conn_close(server->conns);
    }
    reap_dead(server);
    wc_mem_free(server);
}
