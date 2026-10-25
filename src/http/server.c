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
#include <sys/epoll.h>
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
    if (conn == NULL) {
        return;
    }
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
    if (conn->h2 != NULL) {
        wc_h2_server_free(conn->h2);
        conn->h2 = NULL;
    }
    wc_arena_destroy(&conn->arena);
    wc_buf_free(&conn->out);
    wc_buf_free(&conn->response.headers);
    free(conn->head);
    wc_mem_free(conn);
}

static void set_timer(wc_http_conn *conn, int timeout_ms) {
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
            (void)wc_loop_mod_fd(conn->server->loop, conn->fd, EPOLLIN | EPOLLOUT);
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
        (void)wc_loop_mod_fd(conn->server->loop, conn->fd, EPOLLIN);
        return true;
    }
    if (conn->closing) {
        wc_http_conn_close(conn);
        return false;
    }
    (void)wc_loop_mod_fd(conn->server->loop, conn->fd, EPOLLIN);
    finish_response(conn);
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
            return;
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

static bool conn_h2_flush(wc_http_conn *conn) {
    for (;;) {
        size_t length = 0;
        const uint8_t *data = wc_h2_server_output(conn->h2, &length);
        if (length == 0) {
            (void)wc_loop_mod_fd(conn->server->loop, conn->fd, EPOLLIN);
            return true;
        }
        struct iovec iov = {.iov_base = (void *)(uintptr_t)data, .iov_len = length};
        ssize_t written = wc_transport_writev(conn->transport, &iov, 1);
        if (written > 0) {
            wc_h2_server_output_consume(conn->h2, (size_t)written);
        } else if (written < 0 && (errno == EAGAIN || errno == EINTR)) {
            (void)wc_loop_mod_fd(conn->server->loop, conn->fd, EPOLLIN | EPOLLOUT);
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
    ssize_t got = wc_transport_read(conn->transport, buffer, sizeof buffer);
    if (got > 0) {
        size_t consumed = 0;
        if (wc_h2_server_feed(conn->h2, buffer, (size_t)got, &consumed) != WC_OK) {
            wc_http_conn_close(conn);
            return;
        }
        if (!conn_h2_flush(conn)) {
            return;
        }
        if (wc_h2_server_closing(conn->h2)) {
            (void)shutdown(conn->fd, SHUT_WR);
            (void)wc_loop_mod_fd(conn->server->loop, conn->fd, EPOLLIN);
        }
    } else if (got == 0) {
        wc_http_conn_close(conn);
    } else if (errno != EAGAIN && errno != EINTR) {
        wc_http_conn_close(conn);
    }
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
            wc_http_conn_close(conn);
            return true;
        }
        conn->head_len = 0;
    }
    (void)conn_h2_flush(conn);
    return true;
}

static void conn_read(wc_http_conn *conn) {
    if (conn->h2 != NULL) {
        conn_h2_read(conn);
        return;
    }
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
        send_simple(conn, 431);
        return;
    }
    ssize_t got = wc_transport_read(conn->transport, conn->head + conn->head_len, space);
    if (got > 0) {
        conn->head_len += (size_t)got;
        if (conn_switch_to_h2(conn)) {
            return;
        }
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
    if ((events & (EPOLLERR | EPOLLHUP)) != 0) {
        wc_http_conn_close(conn);
        return;
    }
    if ((events & EPOLLOUT) != 0 && conn->h2 != NULL) {
        (void)conn_h2_flush(conn);
        return;
    }
    if ((events & EPOLLOUT) != 0 && conn->out.len > conn->out_sent) {
        if (!wc_http_conn_flush(conn)) {
            return;
        }
        conn_process(conn);
        return;
    }
    if ((events & EPOLLIN) != 0) {
        conn_read(conn);
    }
}

static void conn_timer(void *user) {
    wc_http_conn *conn = user;
    conn->timer = NULL;
    if (conn->h2 != NULL) {
        wc_http_conn_close(conn);
        return;
    }
    if (conn->response.streaming) {
        wc_http_conn_close(conn);
    } else if (conn->have_request || conn->head_len > 0) {
        send_simple(conn, 408);
    } else {
        wc_http_conn_close(conn);
    }
}

static wc_http_conn *conn_new(wc_http_server *server, int fd) {
    wc_http_conn *conn = wc_mem_calloc(1, sizeof *conn);
    if (conn == NULL) {
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
    conn->head = malloc(conn->head_cap);
    if (conn->head == NULL) {
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
    if (wc_loop_add_fd(server->loop, fd, EPOLLIN, conn_event, conn) != WC_OK) {
        wc_http_conn_close(conn);
        return NULL;
    }
    set_timer(conn, server->config.idle_timeout_ms);
    return conn;
}

static void listener_event(wc_loop *loop, int fd, uint32_t events, void *user) {
    (void)loop;
    (void)events;
    wc_http_server *server = user;
    for (int i = 0; i < WC_ACCEPT_BURST; i++) {
        int client = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        int one = 1;
        (void)setsockopt(client, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        if (server->conn_count >= server->config.max_connections) {
            close(client);
            continue;
        }
        if (conn_new(server, client) == NULL) {
            close(client);
        }
    }
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
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return WC_EIO;
    }
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (bind_addr == NULL) {
        bind_addr = "127.0.0.1";
    }
    if (inet_pton(AF_INET, bind_addr, &address.sin_addr) != 1) {
        close(fd);
        return WC_EINVAL;
    }
    if (bind(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        close(fd);
        return WC_EIO;
    }
    if (listen(fd, 128) != 0) {
        close(fd);
        return WC_EIO;
    }
    socklen_t length = sizeof address;
    if (getsockname(fd, (struct sockaddr *)&address, &length) != 0) {
        close(fd);
        return WC_EIO;
    }
    server->port = ntohs(address.sin_port);
    if (wc_loop_add_fd(server->loop, fd, EPOLLIN, listener_event, server) != WC_OK) {
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
    if (server->listen_fd >= 0) {
        (void)wc_loop_del_fd(server->loop, server->listen_fd);
        close(server->listen_fd);
        server->listen_fd = -1;
    }
    while (server->conns != NULL) {
        wc_http_conn_close(server->conns);
    }
    wc_mem_free(server);
}
