#include <stddef.h>
#include <stdint.h>

#include "http2/frame.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wc_h2_frame_header header;
    (void)wc_h2_frame_header_parse(data, size, 16384, &header);
    return 0;
}
