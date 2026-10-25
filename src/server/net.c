#include "server/net.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "http/response.h"
#include "server/host_internal.h"

static void abort_text(WrenVM *vm, const char *text) {
    wrenSetSlotString(vm, 0, text);
    wrenAbortFiber(vm, 0);
}

static int slot_index(WrenVM *vm) {
    double value = wrenGetSlotDouble(vm, 1);
    if (value < 0 || value > 1e9) {
        return -1;
    }
    return (int)value;
}

static size_t percent_decode(const char *source, size_t length, char *target, size_t capacity,
                             bool plus_is_space) {
    size_t out = 0;
    for (size_t i = 0; i < length && out + 1 < capacity; i++) {
        unsigned char c = (unsigned char)source[i];
        if (c == '%' && i + 2 < length) {
            int high = source[i + 1] >= '0' && source[i + 1] <= '9'   ? source[i + 1] - '0'
                       : source[i + 1] >= 'a' && source[i + 1] <= 'f' ? source[i + 1] - 'a' + 10
                       : source[i + 1] >= 'A' && source[i + 1] <= 'F' ? source[i + 1] - 'A' + 10
                                                                      : -1;
            int low = source[i + 2] >= '0' && source[i + 2] <= '9'   ? source[i + 2] - '0'
                      : source[i + 2] >= 'a' && source[i + 2] <= 'f' ? source[i + 2] - 'a' + 10
                      : source[i + 2] >= 'A' && source[i + 2] <= 'F' ? source[i + 2] - 'A' + 10
                                                                     : -1;
            if (high >= 0 && low >= 0) {
                target[out++] = (char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        if (plus_is_space && c == '+') {
            target[out++] = ' ';
        } else {
            target[out++] = (char)c;
        }
    }
    target[out] = '\0';
    return out;
}

static void req_method(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    wrenSetSlotBytes(vm, 0, object->request->method.data, object->request->method.len);
}

static void req_path(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    char decoded[WC_HTTP_MAX_REQUEST_LINE];
    size_t length = percent_decode(object->request->path.data, object->request->path.len, decoded,
                                   sizeof decoded, false);
    wrenSetSlotBytes(vm, 0, decoded, length);
}

static void req_http_version(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    const wc_str version = object->request->version;
    if (version.len == 8 && memcmp(version.data, "HTTP/1.1", 8) == 0) {
        wrenSetSlotString(vm, 0, "1.1");
    } else if (version.len == 8 && memcmp(version.data, "HTTP/1.0", 8) == 0) {
        wrenSetSlotString(vm, 0, "1.0");
    } else {
        wrenSetSlotBytes(vm, 0, version.data, version.len);
    }
}

static void req_body(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    if (object->request->body_len == 0) {
        wrenSetSlotString(vm, 0, "");
    } else {
        wrenSetSlotBytes(vm, 0, object->request->body, object->request->body_len);
    }
}

static void req_header_count(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, (double)object->request->header_count);
}

static void req_header_name(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    int index = slot_index(vm);
    if (index < 0 || (size_t)index >= object->request->header_count) {
        abort_text(vm, "header index out of range");
        return;
    }
    const wc_str name = object->request->headers[index].name;
    char lower[256];
    size_t length = name.len < sizeof lower - 1 ? name.len : sizeof lower - 1;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)name.data[i];
        lower[i] = (char)((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
    }
    wrenSetSlotBytes(vm, 0, lower, length);
}

static void req_header_value(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    int index = slot_index(vm);
    if (index < 0 || (size_t)index >= object->request->header_count) {
        abort_text(vm, "header index out of range");
        return;
    }
    const wc_str value = object->request->headers[index].value;
    wrenSetSlotBytes(vm, 0, value.data, value.len);
}

static bool query_pair(const wc_http_request *request, int index, wc_str *key, wc_str *value) {
    size_t start = 0;
    int seen = 0;
    while (start <= request->query.len) {
        size_t end = start;
        while (end < request->query.len && request->query.data[end] != '&') {
            end++;
        }
        wc_str pair = {request->query.data + start, end - start};
        if (pair.len > 0) {
            if (seen == index) {
                const char *equals = memchr(pair.data, '=', pair.len);
                *key = equals == NULL ? pair : (wc_str){pair.data, (size_t)(equals - pair.data)};
                *value = equals == NULL
                             ? (wc_str){"", 0}
                             : (wc_str){equals + 1, pair.len - (size_t)(equals - pair.data) - 1};
                return true;
            }
            seen++;
        }
        if (end == request->query.len) {
            break;
        }
        start = end + 1;
    }
    return false;
}

static int query_count(const wc_http_request *request) {
    int count = 0;
    size_t start = 0;
    while (start <= request->query.len) {
        size_t end = start;
        while (end < request->query.len && request->query.data[end] != '&') {
            end++;
        }
        if (end > start) {
            count++;
        }
        if (end == request->query.len) {
            break;
        }
        start = end + 1;
    }
    return count;
}

static void req_query_count(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, (double)query_count(object->request));
}

static void req_query_key(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    int index = slot_index(vm);
    wc_str key;
    wc_str value;
    if (!query_pair(object->request, index, &key, &value)) {
        abort_text(vm, "query index out of range");
        return;
    }
    char decoded[8192];
    size_t length = percent_decode(key.data, key.len, decoded, sizeof decoded, true);
    wrenSetSlotBytes(vm, 0, decoded, length);
}

static void req_query_value(WrenVM *vm) {
    wc_req_object *object = wrenGetSlotForeign(vm, 0);
    int index = slot_index(vm);
    wc_str key;
    wc_str value;
    if (!query_pair(object->request, index, &key, &value)) {
        abort_text(vm, "query index out of range");
        return;
    }
    char decoded[8192];
    size_t length = percent_decode(value.data, value.len, decoded, sizeof decoded, true);
    wrenSetSlotBytes(vm, 0, decoded, length);
}

static void res_send(WrenVM *vm) {
    wc_res_object *object = wrenGetSlotForeign(vm, 0);
    double status_number = wrenGetSlotDouble(vm, 1);
    if (status_number < 100 || status_number > 599) {
        abort_text(vm, "status must be between 100 and 599");
        return;
    }
    if (wrenGetSlotType(vm, 2) != WREN_TYPE_STRING) {
        abort_text(vm, "body must be a String");
        return;
    }
    int length = 0;
    const char *body = wrenGetSlotBytes(vm, 2, &length);
    wc_status result =
        wc_http_response_send(object->response, (int)status_number, body, (size_t)length);
    if (result != WC_OK) {
        abort_text(vm, result == WC_EOVERFLOW ? "response body exceeds max_response_bytes"
                                              : "response already sent or invalid");
    }
}

static void res_header(WrenVM *vm) {
    wc_res_object *object = wrenGetSlotForeign(vm, 0);
    int name_length = 0;
    int value_length = 0;
    const char *name = wrenGetSlotBytes(vm, 1, &name_length);
    const char *value = wrenGetSlotBytes(vm, 2, &value_length);
    if (wc_http_response_set_header(object->response, name, (size_t)name_length, value,
                                    (size_t)value_length) != WC_OK) {
        abort_text(vm, "invalid or duplicate response header");
    }
}

static void res_sent(WrenVM *vm) {
    wc_res_object *object = wrenGetSlotForeign(vm, 0);
    wrenSetSlotBool(vm, 0, wc_http_response_sent(object->response));
}

static void res_sse_start(WrenVM *vm) {
    wc_res_object *object = wrenGetSlotForeign(vm, 0);
    int length = 0;
    const char *type = wrenGetSlotBytes(vm, 1, &length);
    if (type == NULL || length <= 0) {
        abort_text(vm, "sseStart: content type required");
        return;
    }
    char content_type[128];
    if ((size_t)length >= sizeof content_type) {
        abort_text(vm, "sseStart: content type too long");
        return;
    }
    memcpy(content_type, type, (size_t)length);
    content_type[length] = '\0';
    if (wc_http_response_stream_start(object->response, 200, content_type) != WC_OK) {
        abort_text(vm, "sseStart: HTTP/2 or invalid response state");
    }
}

static void res_sse_send(WrenVM *vm) {
    wc_res_object *object = wrenGetSlotForeign(vm, 0);
    int event_len = 0;
    int data_len = 0;
    const char *event = wrenGetSlotBytes(vm, 1, &event_len);
    const char *data = wrenGetSlotBytes(vm, 2, &data_len);
    if (event == NULL || data == NULL) {
        abort_text(vm, "sseSend: event and data must be Strings");
        return;
    }
    char event_text[257];
    char data_text[65537];
    if ((size_t)event_len >= sizeof event_text || (size_t)data_len >= sizeof data_text) {
        abort_text(vm, "sseSend: event or data too long");
        return;
    }
    memcpy(event_text, event, (size_t)event_len);
    event_text[event_len] = '\0';
    memcpy(data_text, data, (size_t)data_len);
    data_text[data_len] = '\0';
    if (wc_http_response_stream_write(object->response, event_text, data_text) != WC_OK) {
        abort_text(vm, "sseSend: stream write failed");
    }
}

static void res_sse_close(WrenVM *vm) {
    wc_res_object *object = wrenGetSlotForeign(vm, 0);
    if (wc_http_response_stream_end(object->response) != WC_OK) {
        abort_text(vm, "sseClose: invalid stream state");
    }
}

static void server_serve(WrenVM *vm) {
    wc_vm *vm_state = wrenGetUserData(vm);
    wc_host *host = wc_vm_bind_user(vm_state);
    if (host == NULL || host->server == NULL) {
        abort_text(vm, "no listener is bound");
        return;
    }
    if (host->handler != NULL) {
        abort_text(vm, "a handler is already installed");
        return;
    }
    host->handler = wrenGetSlotHandle(vm, 1);
    host->handler_call = wrenMakeCallHandle(vm, "call(_,_)");
    wrenGetVariable(vm, "net", "Req", 0);
    host->req_class = wrenGetSlotHandle(vm, 0);
    wrenGetVariable(vm, "net", "Res", 0);
    host->res_class = wrenGetSlotHandle(vm, 0);
    if (host->handler == NULL || host->handler_call == NULL || host->req_class == NULL ||
        host->res_class == NULL) {
        abort_text(vm, "could not install handler");
        return;
    }
    host->running = true;
}

static void server_stop(WrenVM *vm) {
    wc_vm *vm_state = wrenGetUserData(vm);
    wc_host *host = wc_vm_bind_user(vm_state);
    if (host == NULL) {
        return;
    }
    host->running = false;
    wc_host_stop(host);
}

static void server_running(WrenVM *vm) {
    wc_vm *vm_state = wrenGetUserData(vm);
    wc_host *host = wc_vm_bind_user(vm_state);
    wrenSetSlotBool(vm, 0, host != NULL && host->running);
}

WrenForeignMethodFn wc_net_bind_method(void *user, const char *module, const char *class_name,
                                       bool is_static, const char *signature) {
    (void)user;
    if (strcmp(module, "net") != 0) {
        return NULL;
    }
    if (strcmp(class_name, "Server") == 0 && is_static) {
        if (strcmp(signature, "serve(_)") == 0) {
            return server_serve;
        }
        if (strcmp(signature, "stop()") == 0) {
            return server_stop;
        }
        if (strcmp(signature, "running") == 0) {
            return server_running;
        }
    }
    if (strcmp(class_name, "Req") == 0 && !is_static) {
        if (strcmp(signature, "method") == 0) {
            return req_method;
        }
        if (strcmp(signature, "path") == 0) {
            return req_path;
        }
        if (strcmp(signature, "body") == 0) {
            return req_body;
        }
        if (strcmp(signature, "httpVersion") == 0) {
            return req_http_version;
        }
        if (strcmp(signature, "rawHeaderCount") == 0) {
            return req_header_count;
        }
        if (strcmp(signature, "rawHeaderName(_)") == 0) {
            return req_header_name;
        }
        if (strcmp(signature, "rawHeaderValue(_)") == 0) {
            return req_header_value;
        }
        if (strcmp(signature, "rawQueryCount") == 0) {
            return req_query_count;
        }
        if (strcmp(signature, "rawQueryKey(_)") == 0) {
            return req_query_key;
        }
        if (strcmp(signature, "rawQueryValue(_)") == 0) {
            return req_query_value;
        }
    }
    if (strcmp(class_name, "Res") == 0 && !is_static) {
        if (strcmp(signature, "send(_,_)") == 0) {
            return res_send;
        }
        if (strcmp(signature, "header(_,_)") == 0) {
            return res_header;
        }
        if (strcmp(signature, "sent") == 0) {
            return res_sent;
        }
        if (strcmp(signature, "sseStart(_)") == 0) {
            return res_sse_start;
        }
        if (strcmp(signature, "sseSend(_,_)") == 0) {
            return res_sse_send;
        }
        if (strcmp(signature, "sseClose()") == 0) {
            return res_sse_close;
        }
    }
    return NULL;
}

WrenForeignClassMethods wc_net_bind_class(void *user, const char *module, const char *class_name) {
    (void)user;
    (void)module;
    (void)class_name;
    WrenForeignClassMethods methods = {NULL, NULL};
    return methods;
}
