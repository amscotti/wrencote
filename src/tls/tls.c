#include "tls/tls.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

struct wc_tls_context {
    SSL_CTX *ctx;
    bool http2;
};

struct wc_tls_conn {
    SSL *ssl;
    char alpn[16];
};

static int select_alpn(SSL *ssl, const unsigned char **out, unsigned char *out_len,
                       const unsigned char *in, unsigned int in_len, void *arg) {
    (void)ssl;
    const wc_tls_context *context = arg;
    bool h2 = false;
    bool http1 = false;
    for (unsigned int i = 0; i < in_len;) {
        unsigned int length = in[i];
        if (length == 0 || i + 1 + length > in_len) {
            break;
        }
        if (length == 2 && memcmp(in + i + 1, "h2", 2) == 0) {
            h2 = true;
        } else if (length == 8 && memcmp(in + i + 1, "http/1.1", 8) == 0) {
            http1 = true;
        }
        i += 1 + length;
    }
    static const unsigned char h2_protocol[] = "h2";
    static const unsigned char http1_protocol[] = "http/1.1";
    if (h2 && context != NULL && context->http2) {
        *out = h2_protocol;
        *out_len = 2;
        return SSL_TLSEXT_ERR_OK;
    }
    if (http1) {
        *out = http1_protocol;
        *out_len = 8;
        return SSL_TLSEXT_ERR_OK;
    }
    /* No protocol overlap: RFC 7301 §3.2 requires a fatal
     * no_application_protocol alert rather than continuing without ALPN. */
    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

wc_status wc_tls_context_new(const char *cert_path, const char *key_path, wc_tls_context **out) {
    if (cert_path == NULL || key_path == NULL || out == NULL) {
        return WC_EINVAL;
    }
    wc_tls_context *context = calloc(1, sizeof *context);
    if (context == NULL) {
        return WC_ENOMEM;
    }
    context->ctx = SSL_CTX_new(TLS_server_method());
    if (context->ctx == NULL) {
        free(context);
        return WC_ENOMEM;
    }
    if (SSL_CTX_set_min_proto_version(context->ctx, TLS1_2_VERSION) != 1 ||
        SSL_CTX_set_max_proto_version(context->ctx, TLS1_3_VERSION) != 1) {
        wc_tls_context_free(context);
        return WC_EINVAL;
    }
    SSL_CTX_set_options(context->ctx, SSL_OP_NO_COMPRESSION | SSL_OP_NO_RENEGOTIATION);
    /* Partial writes may return short success; the moving-buffer mode lets a
     * retry continue from a different pointer (the h2 output buffer shifts).
     * Without it OpenSSL raises BAD_WRITE_RETRY and drops the connection. */
    SSL_CTX_set_mode(context->ctx, SSL_MODE_ENABLE_PARTIAL_WRITE |
                                       SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER |
                                       SSL_MODE_RELEASE_BUFFERS);
    /* No resumption is configured at the TLS 1.2 session-ID level, so keep the
     * default session cache (about 20k sessions) from becoming a memory
     * ceiling under handshake floods. TLS 1.3 tickets remain stateless. */
    SSL_CTX_set_session_cache_mode(context->ctx, SSL_SESS_CACHE_OFF);
    SSL_CTX_set_alpn_select_cb(context->ctx, select_alpn, context);
    if (SSL_CTX_use_certificate_chain_file(context->ctx, cert_path) != 1 ||
        SSL_CTX_use_PrivateKey_file(context->ctx, key_path, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(context->ctx) != 1) {
        wc_tls_context_free(context);
        return WC_EINVAL;
    }
    *out = context;
    return WC_OK;
}

void wc_tls_context_enable_http2(wc_tls_context *context, bool enable) {
    if (context != NULL) {
        context->http2 = enable;
    }
}

void wc_tls_context_free(wc_tls_context *context) {
    if (context == NULL) {
        return;
    }
    SSL_CTX_free(context->ctx);
    free(context);
}

wc_status wc_tls_conn_new(wc_tls_context *context, int fd, wc_tls_conn **out) {
    if (context == NULL || fd < 0 || out == NULL) {
        return WC_EINVAL;
    }
    wc_tls_conn *conn = calloc(1, sizeof *conn);
    if (conn == NULL) {
        return WC_ENOMEM;
    }
    conn->ssl = SSL_new(context->ctx);
    if (conn->ssl == NULL) {
        free(conn);
        return WC_ENOMEM;
    }
    if (SSL_set_fd(conn->ssl, fd) != 1) {
        SSL_free(conn->ssl);
        free(conn);
        return WC_EIO;
    }
    SSL_set_accept_state(conn->ssl);
    *out = conn;
    return WC_OK;
}

void wc_tls_conn_free(wc_tls_conn *conn) {
    if (conn == NULL) {
        return;
    }
    SSL_free(conn->ssl);
    free(conn);
}

static wc_tls_result map_error(SSL *ssl, int result) {
    int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_WANT_READ) {
        return WC_TLS_WANT_READ;
    }
    if (error == SSL_ERROR_WANT_WRITE) {
        return WC_TLS_WANT_WRITE;
    }
    if (error == SSL_ERROR_ZERO_RETURN) {
        return WC_TLS_CLOSED;
    }
    return WC_TLS_ERROR;
}

wc_tls_result wc_tls_handshake(wc_tls_conn *conn) {
    if (conn == NULL) {
        return WC_TLS_ERROR;
    }
    int result = SSL_accept(conn->ssl);
    if (result == 1) {
        const unsigned char *selected = NULL;
        unsigned int length = 0;
        SSL_get0_alpn_selected(conn->ssl, &selected, &length);
        if (selected != NULL && length < sizeof conn->alpn) {
            memcpy(conn->alpn, selected, length);
            conn->alpn[length] = '\0';
        }
        return WC_TLS_OK;
    }
    return map_error(conn->ssl, result);
}

wc_tls_result wc_tls_read(wc_tls_conn *conn, void *buffer, size_t capacity, size_t *out_len) {
    if (conn == NULL || buffer == NULL || out_len == NULL || capacity == 0) {
        return WC_TLS_ERROR;
    }
    int result = SSL_read(conn->ssl, buffer, (int)capacity);
    if (result > 0) {
        *out_len = (size_t)result;
        return WC_TLS_OK;
    }
    *out_len = 0;
    return map_error(conn->ssl, result);
}

wc_tls_result wc_tls_write(wc_tls_conn *conn, const void *buffer, size_t length, size_t *out_len) {
    if (conn == NULL || buffer == NULL || out_len == NULL) {
        return WC_TLS_ERROR;
    }
    int result = SSL_write(conn->ssl, buffer, (int)length);
    if (result > 0) {
        *out_len = (size_t)result;
        return WC_TLS_OK;
    }
    *out_len = 0;
    return map_error(conn->ssl, result);
}

const char *wc_tls_alpn(const wc_tls_conn *conn) {
    return conn == NULL || conn->alpn[0] == '\0' ? NULL : conn->alpn;
}
