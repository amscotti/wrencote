#ifndef WC_HTTP2_FRAME_H
#define WC_HTTP2_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "core/status.h"

#define WC_H2_FRAME_HEADER_SIZE 9u
#define WC_H2_DEFAULT_MAX_FRAME_SIZE 16384u
#define WC_H2_MAX_FRAME_SIZE 16777215u

typedef enum wc_h2_frame_type {
    WC_H2_DATA = 0x0,
    WC_H2_HEADERS = 0x1,
    WC_H2_PRIORITY = 0x2,
    WC_H2_RST_STREAM = 0x3,
    WC_H2_SETTINGS = 0x4,
    WC_H2_PUSH_PROMISE = 0x5,
    WC_H2_PING = 0x6,
    WC_H2_GOAWAY = 0x7,
    WC_H2_WINDOW_UPDATE = 0x8,
    WC_H2_CONTINUATION = 0x9
} wc_h2_frame_type;

typedef struct wc_h2_frame_header {
    uint32_t length;
    uint8_t type;
    uint8_t flags;
    uint32_t stream_id;
} wc_h2_frame_header;

/* Parses the fixed 9-byte header. The reserved stream-id bit is ignored. */
wc_status wc_h2_frame_header_parse(const uint8_t *data, size_t available, uint32_t max_frame_size,
                                   wc_h2_frame_header *out);

#endif
