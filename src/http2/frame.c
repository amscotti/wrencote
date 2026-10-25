#include "http2/frame.h"

wc_status wc_h2_frame_header_parse(const uint8_t *data, size_t available, uint32_t max_frame_size,
                                   wc_h2_frame_header *out) {
    if (data == NULL || out == NULL) {
        return WC_EINVAL;
    }
    if (available < WC_H2_FRAME_HEADER_SIZE) {
        return WC_EAGAIN;
    }
    uint32_t length = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
    if (max_frame_size == 0) {
        max_frame_size = WC_H2_DEFAULT_MAX_FRAME_SIZE;
    }
    if (max_frame_size > WC_H2_MAX_FRAME_SIZE) {
        return WC_EINVAL;
    }
    if (length > max_frame_size) {
        return WC_EPROTO;
    }
    out->length = length;
    out->type = data[3];
    out->flags = data[4];
    out->stream_id = (((uint32_t)data[5] << 24) | ((uint32_t)data[6] << 16) |
                      ((uint32_t)data[7] << 8) | data[8]) &
                     0x7fffffffu;
    return WC_OK;
}
