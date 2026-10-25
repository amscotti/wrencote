#ifndef WC_HTTP_LIMITS_H
#define WC_HTTP_LIMITS_H

/* Every value is a compile-time constant so tests and parsers share one source. */
enum {
    WC_HTTP_MAX_REQUEST_LINE = 8 * 1024,
    WC_HTTP_MAX_HEADER_BYTES = 32 * 1024,
    WC_HTTP_MAX_HEADERS = 100,
    WC_HTTP_MAX_BODY = 1024 * 1024,
    WC_HTTP_MAX_RESPONSE = 1024 * 1024,
    WC_HTTP_MAX_CONNECTIONS = 1024,
    WC_HTTP_KEEPALIVE_REQUESTS = 1000,
    WC_HTTP_IDLE_TIMEOUT_MS = 5000,
    WC_HTTP_REQUEST_TIMEOUT_MS = 10000
};

#endif
