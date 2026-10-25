#ifndef WC_HTTP_HTTP_H
#define WC_HTTP_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/str.h"
#include "http/limits.h"

typedef struct wc_http_header {
    wc_str name;
    wc_str value;
} wc_http_header;

typedef struct wc_http_request {
    wc_str method;
    wc_str target;
    wc_str path;
    wc_str query;
    wc_str version;
    wc_http_header headers[WC_HTTP_MAX_HEADERS];
    size_t header_count;
    size_t content_length;
    size_t head_len;
    const char *body;
    size_t body_len;
    bool keep_alive;
    bool connection_close;
    bool has_host;
} wc_http_request;

typedef enum wc_http_parse_result {
    WC_HTTP_PARSE_MORE = 0,
    WC_HTTP_PARSE_OK = 1,
    WC_HTTP_PARSE_BAD = 2
} wc_http_parse_result;

/* Parses one complete head (through CRLFCRLF). Returns MORE when the terminator
 * is absent, BAD with [status] set to 400, 413, or 431. No allocation. */
wc_http_parse_result wc_http_parse_request(const char *data, size_t len, wc_http_request *request,
                                           int *status);
wc_http_parse_result wc_http_parse_request_limit(const char *data, size_t len,
                                                 wc_http_request *request, int *status,
                                                 size_t max_body);

/* Case-insensitive header lookup. Returns NULL when absent. */
const wc_str *wc_http_request_header(const wc_http_request *request, const char *name);

#endif
