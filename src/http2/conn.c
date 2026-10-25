#include "http2/conn.h"

#include <string.h>

#include "core/buf.h"
#include "core/mem.h"
#include "http2/frame.h"

#define WC_H2_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define WC_H2_PREFACE_LEN 24u

struct wc_h2_conn {
    bool preface_seen;
    bool closed;
    uint32_t max_frame_size;
    uint32_t max_concurrent_streams;
    wc_buf output;
};

static void append_frame(wc_h2_conn *conn, uint8_t type, uint8_t flags, uint32_t stream_id,
                         const uint8_t *payload, size_t length) {
    uint8_t header[WC_H2_FRAME_HEADER_SIZE] = {
        (uint8_t)((length >> 16) & 0xff),
        (uint8_t)((length >> 8) & 0xff),
        (uint8_t)(length & 0xff),
        type,
        flags,
        (uint8_t)((stream_id >> 24) & 0x7f),
        (uint8_t)((stream_id >> 16) & 0xff),
        (uint8_t)((stream_id >> 8) & 0xff),
        (uint8_t)(stream_id & 0xff),
    };
    (void)wc_buf_append(&conn->output, header, sizeof header);
    if (length > 0) {
        (void)wc_buf_append(&conn->output, payload, length);
    }
}

wc_h2_conn *wc_h2_conn_new(const wc_h2_conn_config *config) {
    wc_h2_conn *conn = wc_mem_calloc(1, sizeof *conn);
    if (conn == NULL) {
        return NULL;
    }
    conn->max_frame_size = config != NULL && config->max_frame_size > 0
                               ? config->max_frame_size
                               : WC_H2_DEFAULT_MAX_FRAME_SIZE;
    conn->max_concurrent_streams = config != NULL ? config->max_concurrent_streams : 100;
    wc_buf_init(&conn->output);
    append_frame(conn, WC_H2_SETTINGS, 0, 0, NULL, 0);
    return conn;
}

void wc_h2_conn_free(wc_h2_conn *conn) {
    if (conn == NULL) {
        return;
    }
    wc_buf_free(&conn->output);
    wc_mem_free(conn);
}

static wc_status handle_settings(wc_h2_conn *conn, const wc_h2_frame_header *header,
                                 const uint8_t *payload) {
    (void)payload;
    if (header->stream_id != 0) {
        return WC_EPROTO;
    }
    if ((header->flags & 0x1u) != 0) {
        return header->length == 0 ? WC_OK : WC_EPROTO;
    }
    if ((header->length % 6u) != 0) {
        return WC_EPROTO;
    }
    append_frame(conn, WC_H2_SETTINGS, 0x1u, 0, NULL, 0);
    return WC_OK;
}

static wc_status handle_frame(wc_h2_conn *conn, const wc_h2_frame_header *header,
                              const uint8_t *payload) {
    switch (header->type) {
    case WC_H2_SETTINGS:
        return handle_settings(conn, header, payload);
    case WC_H2_PING:
        if (header->stream_id != 0 || header->length != 8) {
            return WC_EPROTO;
        }
        if ((header->flags & 0x1u) == 0) {
            append_frame(conn, WC_H2_PING, 0x1u, 0, payload, 8);
        }
        return WC_OK;
    case WC_H2_WINDOW_UPDATE: {
        if (header->length != 4) {
            return WC_EPROTO;
        }
        uint32_t increment = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                             ((uint32_t)payload[2] << 8) | payload[3];
        return (increment & 0x7fffffffu) == 0 ? WC_EPROTO : WC_OK;
    }
    case WC_H2_GOAWAY:
        if (header->stream_id != 0 || header->length < 8) {
            return WC_EPROTO;
        }
        conn->closed = true;
        return WC_OK;
    case WC_H2_HEADERS:
    case WC_H2_CONTINUATION:
    case WC_H2_DATA:
        return WC_EPROTO;
    default:
        return WC_OK;
    }
}

wc_status wc_h2_conn_feed(wc_h2_conn *conn, const uint8_t *data, size_t len, size_t *consumed) {
    if (conn == NULL || data == NULL || consumed == NULL) {
        return WC_EINVAL;
    }
    size_t offset = 0;
    if (!conn->preface_seen) {
        if (len < WC_H2_PREFACE_LEN) {
            *consumed = 0;
            return WC_EAGAIN;
        }
        if (memcmp(data, WC_H2_PREFACE, WC_H2_PREFACE_LEN) != 0) {
            return WC_EPROTO;
        }
        conn->preface_seen = true;
        offset = WC_H2_PREFACE_LEN;
    }
    while (len - offset >= WC_H2_FRAME_HEADER_SIZE) {
        wc_h2_frame_header header;
        wc_status status =
            wc_h2_frame_header_parse(data + offset, len - offset, conn->max_frame_size, &header);
        if (status != WC_OK) {
            *consumed = offset;
            return status;
        }
        size_t needed = WC_H2_FRAME_HEADER_SIZE + header.length;
        if (len - offset < needed) {
            *consumed = offset;
            return WC_EAGAIN;
        }
        status = handle_frame(conn, &header, data + offset + WC_H2_FRAME_HEADER_SIZE);
        if (status != WC_OK) {
            *consumed = offset;
            return status;
        }
        offset += needed;
    }
    *consumed = offset;
    return WC_OK;
}

const uint8_t *wc_h2_conn_output(const wc_h2_conn *conn, size_t *len) {
    if (conn == NULL || len == NULL) {
        return NULL;
    }
    *len = conn->output.len;
    return conn->output.data;
}

void wc_h2_conn_output_consume(wc_h2_conn *conn, size_t len) {
    if (conn == NULL || len == 0) {
        return;
    }
    if (len >= conn->output.len) {
        conn->output.len = 0;
        return;
    }
    memmove(conn->output.data, conn->output.data + len, conn->output.len - len);
    conn->output.len -= len;
}

bool wc_h2_conn_closed(const wc_h2_conn *conn) {
    return conn != NULL && conn->closed;
}
