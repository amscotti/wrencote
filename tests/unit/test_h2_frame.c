#include <assert.h>
#include <string.h>

#include "http2/frame.h"

int main(void) {
    uint8_t headers[] = {0x00, 0x00, 0x05, 0x01, 0x04, 0x00, 0x00, 0x00, 0x01};
    wc_h2_frame_header frame;
    assert(wc_h2_frame_header_parse(headers, sizeof headers, 0, &frame) == WC_OK);
    assert(frame.length == 5);
    assert(frame.type == WC_H2_HEADERS);
    assert(frame.flags == 0x04);
    assert(frame.stream_id == 1);

    uint8_t reserved[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x01};
    assert(wc_h2_frame_header_parse(reserved, sizeof reserved, 0, &frame) == WC_OK);
    assert(frame.stream_id == 1);

    uint8_t large[] = {0x00, 0x40, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert(wc_h2_frame_header_parse(large, sizeof large, 0, &frame) == WC_EPROTO);
    assert(wc_h2_frame_header_parse(large, sizeof large, WC_H2_MAX_FRAME_SIZE, &frame) == WC_OK);
    assert(frame.length == 16385);

    assert(wc_h2_frame_header_parse(headers, 8, 0, &frame) == WC_EAGAIN);
    assert(wc_h2_frame_header_parse(NULL, sizeof headers, 0, &frame) == WC_EINVAL);
    assert(wc_h2_frame_header_parse(headers, sizeof headers, 0, NULL) == WC_EINVAL);
    assert(wc_h2_frame_header_parse(headers, sizeof headers, WC_H2_MAX_FRAME_SIZE + 1, &frame) ==
           WC_EINVAL);
    return 0;
}
