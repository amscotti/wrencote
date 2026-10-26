#include "modules/http.h"

#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/buf.h"
#include "vm/vm.h"

#define WC_HTTP_CLIENT_MAX_BODY (8u * 1024u * 1024u)
#define WC_HTTP_CLIENT_MAX_LINES 10000u

/* Bounds for loop-blocking calls: every Http request parks the single-threaded
 * event loop until it completes, so the default must be small enough that one
 * dead origin cannot stall every connection for a minute. Scripts doing bulk
 * work can raise it with Http.setTimeout. */
#define WC_HTTP_TIMEOUT_MIN_MS 50L
#define WC_HTTP_TIMEOUT_MAX_MS 300000L
#define WC_HTTP_TIMEOUT_DEFAULT_MS 10000L
#define WC_HTTP_CONNECT_TIMEOUT_S 5L

static unsigned http_refs;
static long http_timeout_ms = WC_HTTP_TIMEOUT_DEFAULT_MS;

typedef struct http_lines {
    char **items;
    size_t count;
    size_t capacity;
    wc_buf pending;
} http_lines;

typedef struct http_result {
    wc_buf body;
    http_lines lines;
    bool collect_lines;
} http_result;

static void http_abort(WrenVM *vm, const char *message) {
    wrenSetSlotString(vm, 0, message);
    wrenAbortFiber(vm, 0);
}

static bool http_string(WrenVM *vm, int slot, const char **data, size_t *length) {
    if (wrenGetSlotType(vm, slot) != WREN_TYPE_STRING) {
        return false;
    }
    int size = 0;
    *data = wrenGetSlotBytes(vm, slot, &size);
    *length = (size_t)size;
    return true;
}

static bool lines_add(http_lines *lines, const char *data, size_t length) {
    if (lines->count >= WC_HTTP_CLIENT_MAX_LINES) {
        return false;
    }
    if (lines->count == lines->capacity) {
        size_t next = lines->capacity == 0 ? 16 : lines->capacity * 2;
        char **items = realloc(lines->items, next * sizeof *items);
        if (items == NULL) {
            return false;
        }
        lines->items = items;
        lines->capacity = next;
    }
    char *line = malloc(length + 1);
    if (line == NULL) {
        return false;
    }
    memcpy(line, data, length);
    line[length] = '\0';
    lines->items[lines->count++] = line;
    return true;
}

static size_t http_write(char *data, size_t size, size_t count, void *user) {
    http_result *result = user;
    size_t length = size * count;
    if (!result->collect_lines) {
        if (result->body.len > WC_HTTP_CLIENT_MAX_BODY ||
            length > WC_HTTP_CLIENT_MAX_BODY - result->body.len) {
            return 0;
        }
        return wc_buf_append(&result->body, data, length) == WC_OK ? length : 0;
    }
    if (result->lines.pending.len > WC_HTTP_CLIENT_MAX_BODY ||
        length > WC_HTTP_CLIENT_MAX_BODY - result->lines.pending.len) {
        return 0;
    }
    if (wc_buf_append(&result->lines.pending, data, length) != WC_OK) {
        return 0;
    }
    size_t start = 0;
    for (size_t i = 0; i < result->lines.pending.len; i++) {
        if (result->lines.pending.data[i] != '\n') {
            continue;
        }
        size_t end = i;
        if (end > start && result->lines.pending.data[end - 1] == '\r') {
            end--;
        }
        if (!lines_add(&result->lines, (const char *)result->lines.pending.data + start,
                       end - start)) {
            return 0;
        }
        start = i + 1;
    }
    if (start > 0) {
        memmove(result->lines.pending.data, result->lines.pending.data + start,
                result->lines.pending.len - start);
        result->lines.pending.len -= start;
    }
    return length;
}

static struct curl_slist *http_headers(const char *headers, size_t length) {
    struct curl_slist *list = NULL;
    size_t start = 0;
    while (start < length) {
        size_t end = start;
        while (end < length && headers[end] != '\n') {
            end++;
        }
        size_t line_end = end;
        if (line_end > start && headers[line_end - 1] == '\r') {
            line_end--;
        }
        if (line_end > start) {
            char *line = malloc(line_end - start + 1);
            if (line == NULL) {
                curl_slist_free_all(list);
                return NULL;
            }
            memcpy(line, headers + start, line_end - start);
            line[line_end - start] = '\0';
            struct curl_slist *appended = curl_slist_append(list, line);
            free(line);
            if (appended == NULL) {
                curl_slist_free_all(list);
                return NULL;
            }
            list = appended;
        }
        start = end + 1;
    }
    return list;
}

static bool http_args(WrenVM *vm, const char **method, size_t *method_len, const char **url,
                      size_t *url_len, const char **headers, size_t *headers_len, const char **body,
                      size_t *body_len) {
    if (!http_string(vm, 1, method, method_len) || !http_string(vm, 2, url, url_len) ||
        !http_string(vm, 3, headers, headers_len)) {
        http_abort(vm, "Http: method, url, and headers must be Strings");
        return false;
    }
    if (wrenGetSlotType(vm, 4) == WREN_TYPE_NULL) {
        *body = "";
        *body_len = 0;
    } else if (!http_string(vm, 4, body, body_len)) {
        http_abort(vm, "Http: body must be a String or null");
        return false;
    }
    return true;
}

static bool http_perform(WrenVM *vm, bool collect_lines, http_result *result) {
    const char *method = NULL;
    const char *url = NULL;
    const char *headers = NULL;
    const char *body = NULL;
    size_t method_len = 0;
    size_t url_len = 0;
    size_t headers_len = 0;
    size_t body_len = 0;
    if (!http_args(vm, &method, &method_len, &url, &url_len, &headers, &headers_len, &body,
                   &body_len)) {
        return false;
    }
    if (url_len < 7 || (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)) {
        http_abort(vm, "Http: only http:// and https:// URLs are allowed");
        return false;
    }
    char *method_text = strndup(method, method_len);
    char *url_text = strndup(url, url_len);
    struct curl_slist *slist = http_headers(headers, headers_len);
    CURL *curl = curl_easy_init();
    if (method_text == NULL || url_text == NULL || (headers_len > 0 && slist == NULL) ||
        curl == NULL) {
        free(method_text);
        free(url_text);
        curl_slist_free_all(slist);
        curl_easy_cleanup(curl);
        http_abort(vm, "Http: out of memory or invalid headers");
        return false;
    }
    wc_buf_init(&result->body);
    wc_buf_init(&result->lines.pending);
    result->collect_lines = collect_lines;
    CURLcode code = CURLE_OK;
    curl_easy_setopt(curl, CURLOPT_URL, url_text);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method_text);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, result);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, http_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, WC_HTTP_CONNECT_TIMEOUT_S);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "wrencote-http/0.3");
    if (body_len > 0) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    }
    code = curl_easy_perform(curl);
    long http_status = 0;
    if (code == CURLE_OK) {
        (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        if (http_status >= 400) {
            code = CURLE_HTTP_RETURNED_ERROR;
        }
    }
    if (code == CURLE_OK && collect_lines && result->lines.pending.len > 0) {
        if (!lines_add(&result->lines, (const char *)result->lines.pending.data,
                       result->lines.pending.len)) {
            code = CURLE_OUT_OF_MEMORY;
        }
    }
    if (code != CURLE_OK) {
        char message[256];
        snprintf(message, sizeof message, "Http: %s", curl_easy_strerror(code));
        http_abort(vm, message);
    }
    curl_easy_cleanup(curl);
    curl_slist_free_all(slist);
    free(method_text);
    free(url_text);
    return code == CURLE_OK;
}

static void http_result_free(http_result *result) {
    wc_buf_free(&result->body);
    wc_buf_free(&result->lines.pending);
    for (size_t i = 0; i < result->lines.count; i++) {
        free(result->lines.items[i]);
    }
    free(result->lines.items);
}

static void http_request(WrenVM *vm) {
    http_result result = {0};
    if (http_perform(vm, false, &result)) {
        wrenSetSlotBytes(vm, 0, (const char *)result.body.data, result.body.len);
    }
    http_result_free(&result);
}

static void http_stream_lines(WrenVM *vm) {
    http_result result = {0};
    if (http_perform(vm, true, &result)) {
        wrenSetSlotNewList(vm, 0);
        for (size_t i = 0; i < result.lines.count; i++) {
            wrenSetSlotBytes(vm, 1, result.lines.items[i], strlen(result.lines.items[i]));
            wrenInsertInList(vm, 0, (int)i, 1);
        }
    }
    http_result_free(&result);
}

static void http_set_timeout(WrenVM *vm) {
    if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM) {
        http_abort(vm, "Http.setTimeout: milliseconds must be a number");
        return;
    }
    double milliseconds = wrenGetSlotDouble(vm, 1);
    if (milliseconds != milliseconds || milliseconds < (double)WC_HTTP_TIMEOUT_MIN_MS ||
        milliseconds > (double)WC_HTTP_TIMEOUT_MAX_MS) {
        http_abort(vm, "Http.setTimeout: milliseconds must be 50..300000");
        return;
    }
    http_timeout_ms = (long)milliseconds;
    wrenSetSlotNull(vm, 0);
}

void wc_http_client_init(void) {
    if (http_refs++ == 0) {
        (void)curl_global_init(CURL_GLOBAL_DEFAULT);
    }
}

void wc_http_client_cleanup(void) {
    if (http_refs > 0 && --http_refs == 0) {
        curl_global_cleanup();
    }
}

WrenForeignMethodFn wc_http_bind_method(void *user, const char *module, const char *class_name,
                                        bool is_static, const char *signature) {
    (void)user;
    if (strcmp(module, "http") != 0 || strcmp(class_name, "Http") != 0 || !is_static) {
        return NULL;
    }
    if (strcmp(signature, "request(_,_,_,_)") == 0) {
        return http_request;
    }
    if (strcmp(signature, "streamLines(_,_,_,_)") == 0) {
        return http_stream_lines;
    }
    if (strcmp(signature, "setTimeout(_)") == 0) {
        return http_set_timeout;
    }
    return NULL;
}
