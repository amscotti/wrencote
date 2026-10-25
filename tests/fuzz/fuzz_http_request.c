#include <stddef.h>
#include <stdint.h>

#include "http/http.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wc_http_request request;
    int status = 0;
    (void)wc_http_parse_request((const char *)data, size, &request, &status);
    return 0;
}
