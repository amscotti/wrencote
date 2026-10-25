#ifndef WC_HTTP2_SERVER_H
#define WC_HTTP2_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/status.h"

typedef struct wc_h2_server wc_h2_server;

typedef struct wc_h2_header {
    const char *name;
    const char *value;
} wc_h2_header;

/* Called once per complete request stream, on the loop thread. The callback must
 * call wc_h2_server_respond before returning. */
typedef void (*wc_h2_request_cb)(void *user, wc_h2_server *server, int32_t stream_id,
                                 const char *method, const char *path, const wc_h2_header *headers,
                                 size_t header_count, const uint8_t *body, size_t body_len);

typedef bool (*wc_h2_flush_fn)(void *user);

wc_h2_server *wc_h2_server_new(wc_h2_request_cb callback, void *user);
void wc_h2_server_set_flush(wc_h2_server *server, wc_h2_flush_fn flush, void *flush_user);
void wc_h2_server_set_limits(wc_h2_server *server, size_t max_body, size_t max_response,
                             size_t max_header);
void wc_h2_server_free(wc_h2_server *server);

/* Consumes bytes from the TLS transport. [consumed] receives how many bytes the
 * session accepted; a caller that sees consumed < len must retain the remainder
 * and pass it again with the next feed. Never returns WC_EAGAIN (nghttp2
 * buffers internally); protocol failures are WC_EPROTO and callback failures
 * WC_EIO. */
wc_status wc_h2_server_feed(wc_h2_server *server, const uint8_t *data, size_t len,
                            size_t *consumed);

wc_status wc_h2_server_respond(wc_h2_server *server, int32_t stream_id, int status,
                               const char *content_type, const uint8_t *body, size_t body_len);

/* Extra response headers; content-type is supplied separately. */
wc_status wc_h2_server_respond_headers(wc_h2_server *server, int32_t stream_id, int status,
                                       const char *content_type, const wc_h2_header *headers,
                                       size_t header_count, const uint8_t *body, size_t body_len);

bool wc_h2_server_closing(const wc_h2_server *server);
const uint8_t *wc_h2_server_output(const wc_h2_server *server, size_t *len);
void wc_h2_server_output_consume(wc_h2_server *server, size_t len);

#endif
