#include <stddef.h>
#include <stdint.h>

#include "http2/server.h"

static void on_request(void *user, wc_h2_server *server, int32_t stream_id, const char *method,
                       const char *path, const wc_h2_header *headers, size_t header_count,
                       const uint8_t *body, size_t body_len) {
    (void)user;
    (void)server;
    (void)stream_id;
    (void)method;
    (void)path;
    (void)headers;
    (void)header_count;
    (void)body;
    (void)body_len;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    wc_h2_server *server = wc_h2_server_new(on_request, NULL);
    if (server == NULL) {
        return 0;
    }
    size_t consumed = 0;
    (void)wc_h2_server_feed(server, data, size, &consumed);
    size_t out_len = 0;
    (void)wc_h2_server_output(server, &out_len);
    wc_h2_server_free(server);
    return 0;
}
