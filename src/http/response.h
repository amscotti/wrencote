#ifndef WC_HTTP_RESPONSE_H
#define WC_HTTP_RESPONSE_H

#include <stdbool.h>
#include <stddef.h>

#include "core/buf.h"
#include "core/status.h"

typedef struct wc_http_conn wc_http_conn;
typedef struct wc_http_response wc_http_response;

/* Adds a header. Names are emitted lowercase; CR/LF in either value is rejected. */
wc_status wc_http_response_set_header(wc_http_response *response, const char *name, size_t name_len,
                                      const char *value, size_t value_len);

/* Builds the complete response into the connection's output buffer. The body is
 * copied; the caller may release it immediately after this returns. */
wc_status wc_http_response_send(wc_http_response *response, int status, const void *body,
                                size_t body_len);

/* SSE/chunked streaming. Only HTTP/1.1; h2 returns WC_EINVAL. */
wc_status wc_http_response_stream_start(wc_http_response *response, int status,
                                        const char *content_type);
wc_status wc_http_response_stream_write(wc_http_response *response, const char *event,
                                        const char *data);
wc_status wc_http_response_stream_end(wc_http_response *response);
bool wc_http_response_streaming(const wc_http_response *response);
void wc_http_response_stream_abort(wc_http_response *response);

bool wc_http_response_sent(const wc_http_response *response);
void wc_http_response_reset(wc_http_response *response);

#endif
