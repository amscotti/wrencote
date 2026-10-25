#ifndef WC_HTTP2_CONN_H
#define WC_HTTP2_CONN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core/status.h"

typedef struct wc_h2_conn wc_h2_conn;

typedef struct wc_h2_conn_config {
    uint32_t max_frame_size;
    uint32_t max_concurrent_streams;
} wc_h2_conn_config;

wc_h2_conn *wc_h2_conn_new(const wc_h2_conn_config *config);
void wc_h2_conn_free(wc_h2_conn *conn);

/* Consumes bytes. Returns EAGAIN for an incomplete frame, EPROTO on a protocol
 * violation, and OK when every complete frame was handled. HEADERS and DATA
 * are rejected until HPACK and stream state land. */
wc_status wc_h2_conn_feed(wc_h2_conn *conn, const uint8_t *data, size_t len, size_t *consumed);

const uint8_t *wc_h2_conn_output(const wc_h2_conn *conn, size_t *len);
void wc_h2_conn_output_consume(wc_h2_conn *conn, size_t len);
bool wc_h2_conn_closed(const wc_h2_conn *conn);

#endif
