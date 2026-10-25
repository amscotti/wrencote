#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <nghttp2/nghttp2.h>

#include "core/buf.h"
#include "http2/server.h"

typedef struct client_capture {
    int status;
    wc_buf body;
    int done;
} client_capture;

typedef struct server_capture {
    int requests;
    char method[16];
    char path[64];
    wc_buf body;
} server_capture;

static int client_header(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name,
                         size_t name_len, const uint8_t *value, size_t value_len, uint8_t flags,
                         void *user_data) {
    (void)session;
    (void)frame;
    (void)flags;
    client_capture *state = user_data;
    if (name_len == 7 && memcmp(name, ":status", 7) == 0) {
        char text[8] = {0};
        size_t copy = value_len < sizeof text - 1 ? value_len : sizeof text - 1;
        memcpy(text, value, copy);
        state->status = atoi(text);
    }
    return 0;
}

static int client_data(nghttp2_session *session, uint8_t flags, int32_t stream_id,
                       const uint8_t *data, size_t len, void *user_data) {
    (void)session;
    (void)flags;
    (void)stream_id;
    client_capture *state = user_data;
    return wc_buf_append(&state->body, data, len) == WC_OK ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

static int client_frame(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    (void)session;
    client_capture *state = user_data;
    if (frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_DATA) {
        if ((frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0) {
            state->done = 1;
        }
    }
    return 0;
}

static ssize_t client_body(nghttp2_session *session, int32_t stream_id, uint8_t *buffer,
                           size_t length, uint32_t *data_flags, nghttp2_data_source *source,
                           void *user_data) {
    (void)session;
    (void)stream_id;
    (void)user_data;
    const char *body = source->ptr;
    size_t amount = strlen(body);
    if (amount > length) {
        amount = length;
    }
    memcpy(buffer, body, amount);
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    return (ssize_t)amount;
}

static void server_request(void *user, wc_h2_server *server, int32_t stream_id, const char *method,
                           const char *path, const wc_h2_header *headers, size_t header_count,
                           const uint8_t *body, size_t body_len) {
    (void)headers;
    (void)header_count;
    server_capture *state = user;
    state->requests++;
    snprintf(state->method, sizeof state->method, "%s", method);
    snprintf(state->path, sizeof state->path, "%s", path);
    assert(wc_buf_append(&state->body, body, body_len) == WC_OK);
    assert(wc_h2_server_respond(server, stream_id, 200, "text/plain", (const uint8_t *)"ok", 2) ==
           WC_OK);
}

int main(void) {
    server_capture server_data = {0};
    wc_buf_init(&server_data.body);
    wc_h2_server *server = wc_h2_server_new(server_request, &server_data);
    assert(server != NULL);

    client_capture client_result = {0};
    wc_buf_init(&client_result.body);
    nghttp2_session_callbacks *callbacks = NULL;
    assert(nghttp2_session_callbacks_new(&callbacks) == 0);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, client_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, client_data);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, client_frame);
    nghttp2_session *client = NULL;
    assert(nghttp2_session_client_new(&client, callbacks, &client_result) == 0);
    nghttp2_session_callbacks_del(callbacks);
    assert(nghttp2_submit_settings(client, NGHTTP2_FLAG_NONE, NULL, 0) == 0);

    uint8_t name_method[] = ":method";
    uint8_t name_scheme[] = ":scheme";
    uint8_t name_authority[] = ":authority";
    uint8_t name_path[] = ":path";
    uint8_t value_method[] = "GET";
    uint8_t value_scheme[] = "https";
    uint8_t value_authority[] = "localhost";
    uint8_t value_path[] = "/health";
    nghttp2_nv request[] = {
        {name_method, value_method, sizeof name_method - 1, sizeof value_method - 1,
         NGHTTP2_NV_FLAG_NONE},
        {name_scheme, value_scheme, sizeof name_scheme - 1, sizeof value_scheme - 1,
         NGHTTP2_NV_FLAG_NONE},
        {name_authority, value_authority, sizeof name_authority - 1, sizeof value_authority - 1,
         NGHTTP2_NV_FLAG_NONE},
        {name_path, value_path, sizeof name_path - 1, sizeof value_path - 1, NGHTTP2_NV_FLAG_NONE},
    };
    assert(nghttp2_submit_request(client, NULL, request, 4, NULL, NULL) > 0);

    for (int i = 0; i < 1000 && !client_result.done; i++) {
        const uint8_t *data = NULL;
        ssize_t length = nghttp2_session_mem_send(client, &data);
        assert(length >= 0);
        if (length > 0) {
            size_t consumed = 0;
            assert(wc_h2_server_feed(server, data, (size_t)length, &consumed) == WC_OK);
            assert(consumed == (size_t)length);
        }
        size_t output_len = 0;
        const uint8_t *output = wc_h2_server_output(server, &output_len);
        if (output_len > 0) {
            ssize_t received = nghttp2_session_mem_recv(client, output, output_len);
            assert(received >= 0);
            assert((size_t)received == output_len);
            wc_h2_server_output_consume(server, output_len);
        }
        usleep(1000);
    }
    assert(client_result.done);
    assert(server_data.requests == 1);
    assert(strcmp(server_data.method, "GET") == 0);
    assert(strcmp(server_data.path, "/health") == 0);
    assert(client_result.status == 200);
    assert(client_result.body.len == 2 && memcmp(client_result.body.data, "ok", 2) == 0);

    wc_buf_clear(&client_result.body);
    client_result.done = 0;
    client_result.status = 0;
    wc_buf_clear(&server_data.body);
    uint8_t post_method[] = "POST";
    uint8_t post_path[] = "/upload";
    uint8_t post_name_method[] = ":method";
    uint8_t post_name_path[] = ":path";
    nghttp2_nv post_request[] = {
        {post_name_method, post_method, sizeof post_name_method - 1, sizeof post_method - 1,
         NGHTTP2_NV_FLAG_NONE},
        {name_scheme, value_scheme, sizeof name_scheme - 1, sizeof value_scheme - 1,
         NGHTTP2_NV_FLAG_NONE},
        {name_authority, value_authority, sizeof name_authority - 1, sizeof value_authority - 1,
         NGHTTP2_NV_FLAG_NONE},
        {post_name_path, post_path, sizeof post_name_path - 1, sizeof post_path - 1,
         NGHTTP2_NV_FLAG_NONE},
    };
    char post_body[] = "data";
    nghttp2_data_provider post_provider = {.source.ptr = post_body, .read_callback = client_body};
    assert(nghttp2_submit_request(client, NULL, post_request, 4, &post_provider, NULL) > 0);
    for (int i = 0; i < 1000 && !client_result.done; i++) {
        const uint8_t *data = NULL;
        ssize_t length = nghttp2_session_mem_send(client, &data);
        assert(length >= 0);
        if (length > 0) {
            size_t consumed = 0;
            assert(wc_h2_server_feed(server, data, (size_t)length, &consumed) == WC_OK);
            assert(consumed == (size_t)length);
        }
        size_t output_len = 0;
        const uint8_t *output = wc_h2_server_output(server, &output_len);
        if (output_len > 0) {
            assert(nghttp2_session_mem_recv(client, output, output_len) >= 0);
            wc_h2_server_output_consume(server, output_len);
        }
        usleep(1000);
    }
    assert(client_result.done);
    assert(server_data.requests == 2);
    assert(strcmp(server_data.method, "POST") == 0);
    assert(strcmp(server_data.path, "/upload") == 0);
    assert(server_data.body.len == 4 && memcmp(server_data.body.data, "data", 4) == 0);

    wc_buf_free(&client_result.body);
    wc_buf_free(&server_data.body);
    wc_h2_server_free(NULL);
    size_t ignored = 0;
    assert(!wc_h2_server_closing(NULL));
    assert(wc_h2_server_output(NULL, &ignored) == NULL);
    assert(wc_h2_server_output(server, NULL) == NULL);
    wc_h2_server_output_consume(NULL, 1);
    size_t consumed = 0;
    assert(wc_h2_server_feed(NULL, (const uint8_t *)"x", 1, &consumed) == WC_EINVAL);
    assert(wc_h2_server_feed(server, NULL, 1, &consumed) == WC_EINVAL);
    assert(wc_h2_server_feed(server, (const uint8_t *)"x", 1, NULL) == WC_EINVAL);
    assert(wc_h2_server_respond(NULL, 1, 200, "text/plain", (const uint8_t *)"x", 1) == WC_EINVAL);
    assert(wc_h2_server_respond(server, 99, 200, "text/plain", (const uint8_t *)"x", 1) ==
           WC_EINVAL);
    assert(wc_h2_server_respond(server, 1, 200, "text/plain", NULL, 1) == WC_EINVAL);
    wc_h2_server *partial = wc_h2_server_new(server_request, &server_data);
    assert(partial != NULL);
    assert(!wc_h2_server_closing(partial));
    size_t partial_consumed = 0;
    assert(wc_h2_server_feed(partial, (const uint8_t *)"PRI * HTTP/2.0", 14, &partial_consumed) ==
           WC_OK);
    wc_h2_server_free(partial);
    wc_h2_server *invalid = wc_h2_server_new(server_request, &server_data);
    assert(invalid != NULL);
    uint8_t zeros[24] = {0};
    assert(wc_h2_server_feed(invalid, zeros, sizeof zeros, &partial_consumed) == WC_EPROTO);
    wc_h2_server_free(invalid);
    nghttp2_session_del(client);
    wc_h2_server_free(server);
    return 0;
}
