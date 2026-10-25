#include "http/http.h"

#include <stdint.h>
#include <string.h>

static bool is_tchar(unsigned char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
        return true;
    default:
        return false;
    }
}

static bool view_equals_ci(wc_str view, const char *text) {
    size_t len = strlen(text);
    return wc_str_eq_ci(view, (wc_str){text, len});
}

static wc_str trim_ows(wc_str view) {
    while (view.len > 0 && (view.data[0] == ' ' || view.data[0] == '\t')) {
        view.data++;
        view.len--;
    }
    while (view.len > 0 && (view.data[view.len - 1] == ' ' || view.data[view.len - 1] == '\t')) {
        view.len--;
    }
    return view;
}

static const char *find_crlf(const char *data, size_t start, size_t len) {
    for (size_t i = start; i + 1 < len; i++) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            return data + i;
        }
    }
    return NULL;
}

static bool parse_content_length(wc_str value, size_t *out) {
    if (value.len == 0) {
        return false;
    }
    size_t total = 0;
    for (size_t i = 0; i < value.len; i++) {
        unsigned char c = (unsigned char)value.data[i];
        if (c < '0' || c > '9') {
            return false;
        }
        unsigned digit = (unsigned)(c - '0');
        if (total > (SIZE_MAX - digit) / 10) {
            return false;
        }
        total = total * 10 + digit;
    }
    *out = total;
    return true;
}

wc_http_parse_result wc_http_parse_request_limit(const char *data, size_t len,
                                                 wc_http_request *request, int *status,
                                                 size_t max_body) {
    if (status != NULL) {
        *status = 400;
    }
    if (data == NULL || request == NULL || len == 0) {
        return WC_HTTP_PARSE_BAD;
    }
    if (len > WC_HTTP_MAX_HEADER_BYTES + WC_HTTP_MAX_REQUEST_LINE) {
        if (status != NULL) {
            *status = 431;
        }
        return WC_HTTP_PARSE_BAD;
    }

    memset(request, 0, sizeof *request);

    const char *line_end = find_crlf(data, 0, len);
    if (line_end == NULL) {
        return len > WC_HTTP_MAX_REQUEST_LINE ? WC_HTTP_PARSE_BAD : WC_HTTP_PARSE_MORE;
    }
    size_t line_len = (size_t)(line_end - data);
    if (line_len > WC_HTTP_MAX_REQUEST_LINE) {
        return WC_HTTP_PARSE_BAD;
    }

    const char *first_sp = memchr(data, ' ', line_len);
    const char *second_sp = NULL;
    if (first_sp != NULL && (size_t)(first_sp - data) + 1 < line_len) {
        second_sp = memchr(first_sp + 1, ' ', line_len - (size_t)(first_sp - data) - 1);
    }
    if (first_sp == NULL || second_sp == NULL) {
        return WC_HTTP_PARSE_BAD;
    }
    if (memchr(second_sp + 1, ' ', line_len - (size_t)(second_sp - data) - 1) != NULL) {
        return WC_HTTP_PARSE_BAD;
    }

    wc_str method = {(const char *)data, (size_t)(first_sp - data)};
    wc_str target = {first_sp + 1, (size_t)(second_sp - first_sp - 1)};
    wc_str version = {second_sp + 1, line_len - (size_t)(second_sp - data) - 1};
    if (method.len == 0) {
        return WC_HTTP_PARSE_BAD;
    }
    for (size_t i = 0; i < method.len; i++) {
        if (!is_tchar((unsigned char)method.data[i])) {
            return WC_HTTP_PARSE_BAD;
        }
    }
    if (target.len == 0 || target.data[0] != '/') {
        return WC_HTTP_PARSE_BAD;
    }
    if (!view_equals_ci(version, "HTTP/1.1") && !view_equals_ci(version, "HTTP/1.0")) {
        return WC_HTTP_PARSE_BAD;
    }

    const char *question = memchr(target.data, '?', target.len);
    if (question != NULL) {
        request->path = (wc_str){target.data, (size_t)(question - target.data)};
        request->query = (wc_str){question + 1, target.len - (size_t)(question - target.data) - 1};
    } else {
        request->path = target;
    }
    if (memchr(target.data, '#', target.len) != NULL) {
        return WC_HTTP_PARSE_BAD;
    }
    request->method = method;
    request->target = target;
    request->version = (wc_str){version.data, version.len};
    request->keep_alive = view_equals_ci(version, "HTTP/1.1");

    size_t pos = line_len + 2;
    size_t content_length = 0;
    bool have_content_length = false;
    bool transfer_encoding = false;

    while (pos < len) {
        const char *header_start = data + pos;
        const char *header_end = find_crlf(data, pos, len);
        if (header_end == NULL) {
            return WC_HTTP_PARSE_MORE;
        }
        size_t header_len = (size_t)(header_end - header_start);
        if (header_len == 0) {
            request->head_len = (size_t)(header_end - data) + 2;
            break;
        }
        if (request->header_count == WC_HTTP_MAX_HEADERS) {
            if (status != NULL) {
                *status = 431;
            }
            return WC_HTTP_PARSE_BAD;
        }
        const char *colon = memchr(header_start, ':', header_len);
        if (colon == NULL || colon == header_start) {
            return WC_HTTP_PARSE_BAD;
        }
        wc_str name = {header_start, (size_t)(colon - header_start)};
        if (name.len > 0 && (name.data[0] == ' ' || name.data[0] == '\t')) {
            return WC_HTTP_PARSE_BAD;
        }
        for (size_t i = 0; i < name.len; i++) {
            if (!is_tchar((unsigned char)name.data[i])) {
                return WC_HTTP_PARSE_BAD;
            }
        }
        wc_str value =
            trim_ows((wc_str){colon + 1, header_len - (size_t)(colon - header_start) - 1});
        for (size_t i = 0; i < value.len; i++) {
            unsigned char c = (unsigned char)value.data[i];
            if ((c < 0x20 && c != '\t') || c == 0x7f) {
                return WC_HTTP_PARSE_BAD;
            }
        }
        request->headers[request->header_count++] = (wc_http_header){name, value};

        if (view_equals_ci(name, "content-length")) {
            size_t parsed = 0;
            if (have_content_length || !parse_content_length(value, &parsed)) {
                return WC_HTTP_PARSE_BAD;
            }
            have_content_length = true;
            content_length = parsed;
        } else if (view_equals_ci(name, "transfer-encoding")) {
            transfer_encoding = true;
        } else if (view_equals_ci(name, "connection")) {
            size_t start = 0;
            while (start <= value.len) {
                size_t end = start;
                while (end < value.len && value.data[end] != ',') {
                    end++;
                }
                wc_str token = trim_ows((wc_str){value.data + start, end - start});
                if (view_equals_ci(token, "close")) {
                    request->keep_alive = false;
                    request->connection_close = true;
                } else if (view_equals_ci(token, "keep-alive")) {
                    request->keep_alive = true;
                }
                if (end == value.len) {
                    break;
                }
                start = end + 1;
            }
        } else if (view_equals_ci(name, "host")) {
            request->has_host = true;
        }
        pos = (size_t)(header_end - data) + 2;
    }

    if (request->head_len == 0) {
        return WC_HTTP_PARSE_MORE;
    }
    if (request->head_len > WC_HTTP_MAX_HEADER_BYTES) {
        if (status != NULL) {
            *status = 431;
        }
        return WC_HTTP_PARSE_BAD;
    }
    if (transfer_encoding) {
        return WC_HTTP_PARSE_BAD;
    }
    if (content_length > max_body) {
        if (status != NULL) {
            *status = 413;
        }
        return WC_HTTP_PARSE_BAD;
    }
    request->content_length = content_length;
    return WC_HTTP_PARSE_OK;
}

const wc_str *wc_http_request_header(const wc_http_request *request, const char *name) {
    if (request == NULL || name == NULL) {
        return NULL;
    }
    wc_str wanted = {name, strlen(name)};
    for (size_t i = 0; i < request->header_count; i++) {
        if (wc_str_eq_ci(request->headers[i].name, wanted)) {
            return &request->headers[i].value;
        }
    }
    return NULL;
}

wc_http_parse_result wc_http_parse_request(const char *data, size_t len, wc_http_request *request,
                                           int *status) {
    return wc_http_parse_request_limit(data, len, request, status, WC_HTTP_MAX_BODY);
}
