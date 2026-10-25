#ifndef WC_TLS_TLS_H
#define WC_TLS_TLS_H

#include <stdbool.h>
#include <stddef.h>

#include "core/status.h"

typedef struct wc_tls_context wc_tls_context;
typedef struct wc_tls_conn wc_tls_conn;

typedef enum wc_tls_result {
    WC_TLS_OK = 0,
    WC_TLS_WANT_READ = 1,
    WC_TLS_WANT_WRITE = 2,
    WC_TLS_CLOSED = 3,
    WC_TLS_ERROR = 4
} wc_tls_result;

/* Loads a certificate chain and key. TLS 1.2+ only, no compression or renegotiation. */
wc_status wc_tls_context_new(const char *cert_path, const char *key_path, wc_tls_context **out);
void wc_tls_context_free(wc_tls_context *context);

/* HTTP/2 is advertised only when the connection layer can serve it. */
void wc_tls_context_enable_http2(wc_tls_context *context, bool enable);

wc_status wc_tls_conn_new(wc_tls_context *context, int fd, wc_tls_conn **out);
void wc_tls_conn_free(wc_tls_conn *conn);

/* Drives the server handshake. Returns WANT_READ/WANT_WRITE until OK. */
wc_tls_result wc_tls_handshake(wc_tls_conn *conn);

wc_tls_result wc_tls_read(wc_tls_conn *conn, void *buffer, size_t capacity, size_t *out_len);
wc_tls_result wc_tls_write(wc_tls_conn *conn, const void *buffer, size_t length, size_t *out_len);

/* Selected ALPN protocol ("h2" or "http/1.1"), or NULL before selection. */
const char *wc_tls_alpn(const wc_tls_conn *conn);

/* Transport factory adapter for wc_http_server_config. */
typedef struct wc_transport wc_transport;
wc_transport *wc_tls_transport_new(void *context, int fd);

#endif
