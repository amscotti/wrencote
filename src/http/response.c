#include "http/internal.h"

#include <stdlib.h>

#include <stdio.h>
#include <string.h>

#include "http/limits.h"

static const char *reason_phrase(int status) {
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 206:
        return "Partial Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 413:
        return "Payload Too Large";
    case 414:
        return "URI Too Long";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    default:
        return "Response";
    }
}

static bool is_tchar(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    return strchr("!#$%&'*+-.^_`|~", c) != NULL && c != '\0';
}

wc_status wc_http_response_set_header(wc_http_response *response, const char *name, size_t name_len,
                                      const char *value, size_t value_len) {
    if (response == NULL || name == NULL || value == NULL || name_len == 0 || name_len > 64 ||
        value_len > 8192 || response->sent) {
        return WC_EINVAL;
    }
    if (response->header_count >= 64) {
        return WC_EINVAL;
    }
    char lower[65];
    for (size_t i = 0; i < name_len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (!is_tchar(c)) {
            return WC_EINVAL;
        }
        lower[i] = (char)((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
    }
    lower[name_len] = '\0';
    if (strcmp(lower, "content-length") == 0 || strcmp(lower, "connection") == 0 ||
        strcmp(lower, "transfer-encoding") == 0) {
        return WC_EINVAL;
    }
    for (size_t i = 0; i < value_len; i++) {
        if (value[i] == '\r' || value[i] == '\n' || value[i] == '\0') {
            return WC_EINVAL;
        }
    }
    wc_status status = wc_buf_appendf(&response->headers, "%s: ", lower);
    if (status == WC_OK) {
        status = wc_buf_append(&response->headers, value, value_len);
    }
    if (status == WC_OK) {
        status = wc_buf_append_cstr(&response->headers, "\r\n");
    }
    if (status != WC_OK) {
        return status;
    }
    if (strcmp(lower, "content-type") == 0) {
        response->has_content_type = true;
    }
    response->header_count++;
    return WC_OK;
}

wc_status wc_http_response_send(wc_http_response *response, int status, const void *body,
                                size_t body_len) {
    if (response == NULL || response->sent) {
        return WC_EINVAL;
    }
    if (status < 100 || status > 599) {
        return WC_EINVAL;
    }
    if (response->h2 != NULL) {
        wc_h2_header extra[64];
        size_t extra_count = 0;
        char type_text[128] = "text/plain; charset=utf-8";
        char *raw = (char *)response->headers.data;
        if (response->headers.cap > response->headers.len) {
            raw[response->headers.len] = '\0';
        }
        char *line = raw;
        char *limit = raw + response->headers.len;
        while (line < limit && extra_count < 64) {
            char *end = strstr(line, "\r\n");
            if (end == NULL) {
                break;
            }
            *end = '\0';
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                *colon = '\0';
                char *value = colon + 1;
                while (*value == ' ') {
                    value++;
                }
                if (strcmp(line, "content-type") == 0) {
                    snprintf(type_text, sizeof type_text, "%s", value);
                } else {
                    extra[extra_count].name = line;
                    extra[extra_count].value = value;
                    extra_count++;
                }
            }
            line = end + 2;
        }
        static const uint8_t empty[] = "";
        wc_status h2_status = wc_h2_server_respond_headers(response->h2, response->h2_stream,
                                                           status, type_text, extra, extra_count,
                                                           body_len == 0 ? empty : body, body_len);
        if (h2_status == WC_OK) {
            response->sent = true;
        }
        return h2_status;
    }
    if (response->conn == NULL || response->conn->server == NULL) {
        return WC_EINVAL;
    }
    if (body_len > response->conn->server->config.max_response_bytes) {
        return WC_EOVERFLOW;
    }
    wc_http_conn *conn = response->conn;
    wc_buf_clear(&conn->out);
    conn->out_sent = 0;

    wc_status result =
        wc_buf_appendf(&conn->out, "HTTP/1.1 %d %s\r\n", status, reason_phrase(status));
    if (result == WC_OK) {
        result = wc_buf_append(&conn->out, response->headers.data, response->headers.len);
    }
    if (result == WC_OK && !response->has_content_type) {
        result = wc_buf_append_cstr(&conn->out, "Content-Type: text/plain; charset=utf-8\r\n");
    }
    if (result == WC_OK) {
        result = wc_buf_appendf(&conn->out, "Content-Length: %zu\r\n", body_len);
    }
    if (result == WC_OK) {
        result = wc_buf_appendf(&conn->out, "Connection: %s\r\n\r\n",
                                conn->closing ? "close" : "keep-alive");
    }
    bool body_allowed = status != 204 && status != 304;
    bool head_request =
        conn->request.method.len == 4 && memcmp(conn->request.method.data, "HEAD", 4) == 0;
    if (result == WC_OK && body_allowed && body_len > 0 && !head_request) {
        result = wc_buf_append(&conn->out, body, body_len);
    }
    if (result != WC_OK) {
        wc_buf_clear(&conn->out);
        return result;
    }
    response->sent = true;
    return WC_OK;
}

static wc_status stream_chunk(wc_http_conn *conn, const void *data, size_t len) {
    size_t pending = conn->out.len - conn->out_sent;
    if (pending > 1024u * 1024u) {
        return WC_EOVERFLOW;
    }
    wc_status status = wc_buf_appendf(&conn->out, "%zx\r\n", len);
    if (status == WC_OK) {
        status = wc_buf_append(&conn->out, data, len);
    }
    if (status == WC_OK) {
        status = wc_buf_append_cstr(&conn->out, "\r\n");
    }
    if (status != WC_OK) {
        wc_buf_clear(&conn->out);
        return status;
    }
    return wc_http_conn_flush(conn) ? WC_OK : WC_EIO;
}

wc_status wc_http_response_stream_start(wc_http_response *response, int status,
                                        const char *content_type) {
    if (response == NULL || response->h2 != NULL || response->conn == NULL || response->sent ||
        content_type == NULL || status < 200 || status > 299) {
        return WC_EINVAL;
    }
    for (const unsigned char *p = (const unsigned char *)content_type; *p != '\0'; p++) {
        if (*p == '\r' || *p == '\n') {
            return WC_EINVAL;
        }
    }
    wc_http_conn *conn = response->conn;
    wc_buf_clear(&conn->out);
    conn->out_sent = 0;
    wc_status result =
        wc_buf_appendf(&conn->out, "HTTP/1.1 %d %s\r\n", status, reason_phrase(status));
    if (result == WC_OK) {
        result = wc_buf_append(&conn->out, response->headers.data, response->headers.len);
    }
    if (result == WC_OK && !response->has_content_type) {
        result = wc_buf_appendf(&conn->out, "Content-Type: %s\r\n", content_type);
    }
    if (result == WC_OK) {
        result = wc_buf_append_cstr(&conn->out, "Cache-Control: no-cache\r\n"
                                                "Transfer-Encoding: chunked\r\n"
                                                "Connection: keep-alive\r\n\r\n");
    }
    if (result != WC_OK) {
        wc_buf_clear(&conn->out);
        return result;
    }
    response->sent = true;
    response->streaming = true;
    wc_http_conn_extend_timeout(conn, 120000);
    if (!wc_http_conn_flush(conn)) {
        response->streaming = false;
        return WC_EIO;
    }
    return WC_OK;
}

wc_status wc_http_response_stream_write(wc_http_response *response, const char *event,
                                        const char *data) {
    if (response == NULL || !response->streaming || response->conn == NULL || event == NULL ||
        data == NULL) {
        return WC_EINVAL;
    }
    size_t event_len = strlen(event);
    size_t data_len = strlen(data);
    if (event_len > 256 || data_len > 65536) {
        return WC_EOVERFLOW;
    }
    if (strpbrk(event, "\r\n") != NULL || strpbrk(data, "\r\n") != NULL) {
        return WC_EINVAL;
    }
    char *frame = malloc(event_len + data_len + 32);
    if (frame == NULL) {
        return WC_ENOMEM;
    }
    int frame_len =
        snprintf(frame, event_len + data_len + 32, "event: %s\ndata: %s\n\n", event, data);
    wc_status result = WC_OK;
    if (frame_len < 0) {
        result = WC_EINVAL;
    } else {
        result = stream_chunk(response->conn, frame, (size_t)frame_len);
    }
    free(frame);
    return result;
}

wc_status wc_http_response_stream_end(wc_http_response *response) {
    if (response == NULL || !response->streaming || response->conn == NULL) {
        return WC_EINVAL;
    }
    wc_http_conn *conn = response->conn;
    if (conn->out.len - conn->out_sent > 1024u * 1024u) {
        return WC_EOVERFLOW;
    }
    wc_status result = wc_buf_append_cstr(&conn->out, "0\r\n\r\n");
    if (result != WC_OK) {
        return result;
    }
    (void)wc_http_conn_flush(conn);
    response->streaming = false;
    conn->closing = true;
    if (conn->timer != NULL) {
        wc_timer_cancel(conn->timer);
        conn->timer = NULL;
    }
    return WC_OK;
}

bool wc_http_response_streaming(const wc_http_response *response) {
    return response != NULL && response->streaming;
}

void wc_http_response_stream_abort(wc_http_response *response) {
    if (response != NULL && response->streaming) {
        response->streaming = false;
        if (response->conn != NULL) {
            response->conn->closing = true;
        }
    }
}

bool wc_http_response_sent(const wc_http_response *response) {
    return response != NULL && response->sent;
}

void wc_http_response_reset(wc_http_response *response) {
    if (response == NULL) {
        return;
    }
    wc_buf_clear(&response->headers);
    response->header_count = 0;
    response->sent = false;
    response->streaming = false;
    response->has_content_type = false;
}
