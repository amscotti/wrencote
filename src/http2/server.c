#include "http2/server.h"
#include "http2/frame.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nghttp2/nghttp2.h>

#include "core/buf.h"
#include "core/mem.h"
#include "http/limits.h"

#define WC_H2_MAX_STREAMS 100u
#define WC_H2_MAX_BODY WC_HTTP_MAX_BODY

typedef struct wc_h2_stream {
    int32_t id;
    char *method;
    char *path;
    wc_buf body;
    wc_buf response;
    size_t sent;
    bool responded;
    bool ready;
    wc_h2_header headers[100];
    size_t header_count;
    struct wc_h2_stream *next;
} wc_h2_stream;

struct wc_h2_server {
    nghttp2_session *session;
    wc_h2_request_cb callback;
    void *user;
    wc_h2_flush_fn flush;
    void *flush_user;
    size_t max_body;
    size_t max_response;
    size_t max_header;
    wc_h2_stream *streams;
    size_t stream_count;
    int32_t last_stream_id;
    bool closing;
    bool scan_preface;
    wc_buf scan;
    wc_buf output;
};

static wc_h2_stream *find_stream(wc_h2_server *server, int32_t id) {
    for (wc_h2_stream *stream = server->streams; stream != NULL; stream = stream->next) {
        if (stream->id == id) {
            return stream;
        }
    }
    return NULL;
}

static wc_h2_stream *create_stream(wc_h2_server *server, int32_t id) {
    if (server->stream_count >= WC_H2_MAX_STREAMS) {
        return NULL;
    }
    wc_h2_stream *stream = wc_mem_calloc(1, sizeof *stream);
    if (stream == NULL) {
        return NULL;
    }
    stream->id = id;
    wc_buf_init(&stream->body);
    wc_buf_init(&stream->response);
    stream->next = server->streams;
    server->streams = stream;
    server->stream_count++;
    return stream;
}

static void free_stream(wc_h2_server *server, wc_h2_stream *stream) {
    wc_h2_stream **link = &server->streams;
    while (*link != NULL) {
        if (*link == stream) {
            *link = stream->next;
            break;
        }
        link = &(*link)->next;
    }
    free(stream->method);
    free(stream->path);
    for (size_t i = 0; i < stream->header_count; i++) {
        free((void *)(uintptr_t)stream->headers[i].name);
        free((void *)(uintptr_t)stream->headers[i].value);
    }
    wc_buf_free(&stream->body);
    wc_buf_free(&stream->response);
    wc_mem_free(stream);
    if (server->stream_count > 0) {
        server->stream_count--;
    }
}

static ssize_t send_callback(nghttp2_session *session, const uint8_t *data, size_t length,
                             int flags, void *user_data) {
    (void)session;
    (void)flags;
    wc_h2_server *server = user_data;
    if (wc_buf_append(&server->output, data, length) != WC_OK) {
        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
    return (ssize_t)length;
}

static int on_begin_headers(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    (void)session;
    wc_h2_server *server = user_data;
    if (frame->hd.type != NGHTTP2_HEADERS) {
        return 0;
    }
    if (find_stream(server, frame->hd.stream_id) == NULL) {
        server->last_stream_id = frame->hd.stream_id;
        if (server->stream_count >= WC_H2_MAX_STREAMS) {
            (void)nghttp2_submit_rst_stream(server->session, NGHTTP2_FLAG_NONE, frame->hd.stream_id,
                                            NGHTTP2_REFUSED_STREAM);
            return 0;
        }
        if (create_stream(server, frame->hd.stream_id) == NULL) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    }
    return 0;
}

static int on_header(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name,
                     size_t name_len, const uint8_t *value, size_t value_len, uint8_t flags,
                     void *user_data) {
    (void)session;
    (void)flags;
    wc_h2_server *server = user_data;
    wc_h2_stream *stream = find_stream(server, frame->hd.stream_id);
    if (stream == NULL) {
        return 0;
    }
    if (name_len == 7 && memcmp(name, ":method", 7) == 0) {
        free(stream->method);
        stream->method = wc_mem_malloc(value_len + 1);
        if (stream->method == NULL) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        memcpy(stream->method, value, value_len);
        stream->method[value_len] = '\0';
    } else if (name_len == 5 && memcmp(name, ":path", 5) == 0) {
        if (value_len > server->max_header) {
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }
        free(stream->path);
        stream->path = wc_mem_malloc(value_len + 1);
        if (stream->path == NULL) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        memcpy(stream->path, value, value_len);
        stream->path[value_len] = '\0';
    } else if (name_len > 0 && name[0] != ':' && stream->header_count < 100) {
        char *header_name = wc_mem_malloc(name_len + 1);
        char *header_value = wc_mem_malloc(value_len + 1);
        if (header_name == NULL || header_value == NULL) {
            free(header_name);
            free(header_value);
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
        memcpy(header_name, name, name_len);
        header_name[name_len] = '\0';
        memcpy(header_value, value, value_len);
        header_value[value_len] = '\0';
        stream->headers[stream->header_count].name = header_name;
        stream->headers[stream->header_count].value = header_value;
        stream->header_count++;
    }
    return 0;
}

static int on_data_chunk_recv(nghttp2_session *session, uint8_t flags, int32_t stream_id,
                              const uint8_t *data, size_t len, void *user_data) {
    (void)session;
    (void)flags;
    wc_h2_server *server = user_data;
    wc_h2_stream *stream = find_stream(server, stream_id);
    if (stream == NULL) {
        return 0;
    }
    if (stream->body.len > server->max_body || len > server->max_body - stream->body.len) {
        nghttp2_submit_rst_stream(server->session, NGHTTP2_FLAG_NONE, stream_id,
                                  NGHTTP2_ENHANCE_YOUR_CALM);
        return 0;
    }
    return wc_buf_append(&stream->body, data, len) == WC_OK ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

static void invoke_handler(wc_h2_server *server, wc_h2_stream *stream) {
    if (stream->responded) {
        return;
    }
    if (stream->method == NULL || stream->path == NULL) {
        (void)wc_h2_server_respond(server, stream->id, 400, "text/plain", (const uint8_t *)"bad\n",
                                   4);
        return;
    }
    if (server->callback != NULL) {
        server->callback(server->user, server, stream->id, stream->method, stream->path,
                         stream->headers, stream->header_count, stream->body.data,
                         stream->body.len);
    }
}

static int on_frame_recv(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
    (void)session;
    wc_h2_server *server = user_data;
    wc_h2_stream *stream = find_stream(server, frame->hd.stream_id);
    if (stream == NULL) {
        return 0;
    }
    if ((frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_DATA) &&
        (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) != 0) {
        stream->ready = true;
    }
    return 0;
}

static int on_stream_close(nghttp2_session *session, int32_t stream_id, uint32_t error_code,
                           void *user_data) {
    (void)session;
    (void)error_code;
    wc_h2_server *server = user_data;
    wc_h2_stream *stream = find_stream(server, stream_id);
    if (stream != NULL) {
        free_stream(server, stream);
    }
    return 0;
}

static ssize_t read_response(nghttp2_session *session, int32_t stream_id, uint8_t *buffer,
                             size_t length, uint32_t *data_flags, nghttp2_data_source *source,
                             void *user_data) {
    (void)session;
    (void)stream_id;
    (void)user_data;
    wc_h2_stream *stream = source->ptr;
    size_t available = stream->response.len - stream->sent;
    size_t amount = available < length ? available : length;
    if (amount > 0) {
        memcpy(buffer, stream->response.data + stream->sent, amount);
        stream->sent += amount;
    }
    if (stream->sent >= stream->response.len) {
        *data_flags |= NGHTTP2_DATA_FLAG_EOF;
    }
    return (ssize_t)amount;
}

wc_h2_server *wc_h2_server_new(wc_h2_request_cb callback, void *user) {
    wc_h2_server *server = wc_mem_calloc(1, sizeof *server);
    if (server == NULL) {
        return NULL;
    }
    server->callback = callback;
    server->user = user;
    server->max_body = WC_H2_MAX_BODY;
    server->max_response = WC_HTTP_MAX_RESPONSE;
    server->max_header = WC_HTTP_MAX_REQUEST_LINE;
    wc_buf_init(&server->output);
    wc_buf_init(&server->scan);

    nghttp2_session_callbacks *callbacks = NULL;
    if (nghttp2_session_callbacks_new(&callbacks) != 0) {
        wc_h2_server_free(server);
        return NULL;
    }
    nghttp2_session_callbacks_set_send_callback(callbacks, send_callback);
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, on_begin_headers);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data_chunk_recv);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close);
    int result = nghttp2_session_server_new(&server->session, callbacks, server);
    nghttp2_session_callbacks_del(callbacks);
    if (result != 0) {
        wc_h2_server_free(server);
        return NULL;
    }
    nghttp2_settings_entry settings[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 100},
        {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, 32768},
    };
    if (nghttp2_submit_settings(server->session, NGHTTP2_FLAG_NONE, settings, 2) != 0 ||
        nghttp2_session_send(server->session) != 0) {
        wc_h2_server_free(server);
        return NULL;
    }
    return server;
}

void wc_h2_server_set_flush(wc_h2_server *server, wc_h2_flush_fn flush, void *flush_user) {
    if (server == NULL) {
        return;
    }
    server->flush = flush;
    server->flush_user = flush_user;
}

void wc_h2_server_set_limits(wc_h2_server *server, size_t max_body, size_t max_response,
                             size_t max_header) {
    if (server == NULL) {
        return;
    }
    server->max_body = max_body;
    server->max_response = max_response;
    server->max_header = max_header;
}

void wc_h2_server_free(wc_h2_server *server) {
    if (server == NULL) {
        return;
    }
    while (server->streams != NULL) {
        free_stream(server, server->streams);
    }
    if (server->session != NULL) {
        nghttp2_session_del(server->session);
        server->session = NULL;
    }
    wc_buf_free(&server->output);
    wc_buf_free(&server->scan);
    wc_mem_free(server);
}

static bool scan_stream_headers(wc_h2_server *server, const uint8_t *data, size_t len) {
    if (wc_buf_append(&server->scan, data, len) != WC_OK) {
        return true;
    }
    size_t offset = 0;
    if (!server->scan_preface) {
        if (server->scan.len < 24) {
            return true;
        }
        memmove(server->scan.data, server->scan.data + 24, server->scan.len - 24);
        server->scan.len -= 24;
        server->scan_preface = true;
    }
    while (server->scan.len - offset >= WC_H2_FRAME_HEADER_SIZE) {
        wc_h2_frame_header header;
        if (wc_h2_frame_header_parse(server->scan.data + offset, server->scan.len - offset,
                                     WC_H2_DEFAULT_MAX_FRAME_SIZE, &header) != WC_OK) {
            break;
        }
        size_t needed = WC_H2_FRAME_HEADER_SIZE + header.length;
        if (server->scan.len - offset < needed) {
            break;
        }
        if (header.type == WC_H2_HEADERS && header.stream_id != 0 &&
            (int32_t)header.stream_id < server->last_stream_id) {
            (void)nghttp2_submit_goaway(server->session, NGHTTP2_FLAG_NONE, server->last_stream_id,
                                        NGHTTP2_PROTOCOL_ERROR, NULL, 0);
            server->closing = true;
            server->scan.len = 0;
            return false;
        }
        if (header.type == WC_H2_HEADERS && header.stream_id != 0) {
            server->last_stream_id = header.stream_id;
        }
        offset += needed;
    }
    if (offset > 0) {
        memmove(server->scan.data, server->scan.data + offset, server->scan.len - offset);
        server->scan.len -= offset;
    }
    return true;
}

wc_status wc_h2_server_feed(wc_h2_server *server, const uint8_t *data, size_t len,
                            size_t *consumed) {
    if (server == NULL || data == NULL || consumed == NULL) {
        return WC_EINVAL;
    }
    if (!scan_stream_headers(server, data, len)) {
        *consumed = len;
        (void)nghttp2_session_send(server->session);
        return WC_OK;
    }
    ssize_t result = nghttp2_session_mem_recv(server->session, data, len);
    if (result < 0) {
        return WC_EPROTO;
    }
    *consumed = (size_t)result;
    if (server->closing || !nghttp2_session_want_read(server->session)) {
        server->closing = true;
        (void)nghttp2_session_send(server->session);
        return WC_OK;
    }
    for (wc_h2_stream *stream = server->streams; stream != NULL;) {
        wc_h2_stream *next = stream->next;
        if (stream->ready) {
            if (next == NULL && server->flush != NULL && !server->flush(server->flush_user)) {
                return WC_EIO;
            }
            stream->ready = false;
            invoke_handler(server, stream);
        }
        stream = next;
    }
    if (nghttp2_session_send(server->session) != 0) {
        return WC_EIO;
    }
    if (!nghttp2_session_want_read(server->session)) {
        server->closing = true;
    }
    return WC_OK;
}

wc_status wc_h2_server_respond_headers(wc_h2_server *server, int32_t stream_id, int status,
                                       const char *content_type, const wc_h2_header *headers,
                                       size_t header_count, const uint8_t *body, size_t body_len) {
    if (server == NULL || body == NULL) {
        return WC_EINVAL;
    }
    if (body_len > server->max_response) {
        return WC_EOVERFLOW;
    }
    wc_h2_stream *stream = find_stream(server, stream_id);
    if (stream == NULL || stream->responded) {
        return WC_EINVAL;
    }
    if (wc_buf_append(&stream->response, body, body_len) != WC_OK) {
        return WC_ENOMEM;
    }
    uint8_t status_name[] = ":status";
    uint8_t type_name[] = "content-type";
    uint8_t length_name[] = "content-length";
    uint8_t status_text[4];
    uint8_t type_text[64];
    uint8_t length_text[32];
    snprintf((char *)status_text, sizeof status_text, "%d", status);
    snprintf((char *)type_text, sizeof type_text, "%s", content_type);
    snprintf((char *)length_text, sizeof length_text, "%zu", body_len);
    nghttp2_nv nv_headers[3 + 64];
    nv_headers[0] = (nghttp2_nv){status_name, status_text, sizeof status_name - 1,
                                 (uint8_t)strlen((char *)status_text), NGHTTP2_NV_FLAG_NONE};
    nv_headers[1] = (nghttp2_nv){type_name, type_text, sizeof type_name - 1,
                                 (uint8_t)strlen((char *)type_text), NGHTTP2_NV_FLAG_NONE};
    nv_headers[2] = (nghttp2_nv){length_name, length_text, sizeof length_name - 1,
                                 (uint8_t)strlen((char *)length_text), NGHTTP2_NV_FLAG_NONE};
    size_t nv_count = 3;
    for (size_t i = 0; i < header_count && nv_count < 67; i++) {
        nv_headers[nv_count++] = (nghttp2_nv){
            (uint8_t *)(uintptr_t)headers[i].name, (uint8_t *)(uintptr_t)headers[i].value,
            strlen(headers[i].name), strlen(headers[i].value), NGHTTP2_NV_FLAG_NONE};
    }
    nghttp2_priority_spec priority;
    nghttp2_priority_spec_init(&priority, 0, body_len > 65536 ? 1 : 256, 0);
    (void)nghttp2_submit_priority(server->session, NGHTTP2_FLAG_NONE, stream_id, &priority);
    nghttp2_data_provider provider = {.source.ptr = stream, .read_callback = read_response};
    int result =
        nghttp2_submit_response(server->session, stream_id, nv_headers, nv_count, &provider);
    if (result != 0) {
        return WC_EIO;
    }
    stream->responded = true;
    if (nghttp2_session_send(server->session) != 0) {
        return WC_EIO;
    }
    return WC_OK;
}

wc_status wc_h2_server_respond(wc_h2_server *server, int32_t stream_id, int status,
                               const char *content_type, const uint8_t *body, size_t body_len) {
    return wc_h2_server_respond_headers(server, stream_id, status, content_type, NULL, 0, body,
                                        body_len);
}

bool wc_h2_server_closing(const wc_h2_server *server) {
    return server != NULL && server->closing;
}

const uint8_t *wc_h2_server_output(const wc_h2_server *server, size_t *len) {
    if (server == NULL || len == NULL) {
        return NULL;
    }
    *len = server->output.len;
    return server->output.data;
}

void wc_h2_server_output_consume(wc_h2_server *server, size_t len) {
    if (server == NULL || len == 0) {
        return;
    }
    if (len >= server->output.len) {
        server->output.len = 0;
        return;
    }
    memmove(server->output.data, server->output.data + len, server->output.len - len);
    server->output.len -= len;
}
