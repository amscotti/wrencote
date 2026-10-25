#include "core/buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "core/checked.h"
#include "core/mem.h"

void wc_buf_init(wc_buf *buffer) {
    if (buffer == NULL) {
        return;
    }
    buffer->data = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

void wc_buf_free(wc_buf *buffer) {
    if (buffer == NULL) {
        return;
    }
    wc_mem_free(buffer->data);
    wc_buf_init(buffer);
}

void wc_buf_clear(wc_buf *buffer) {
    if (buffer == NULL) {
        return;
    }
    buffer->len = 0;
    if (buffer->data != NULL) {
        buffer->data[0] = '\0';
    }
}

WC_NODISCARD wc_status wc_buf_reserve(wc_buf *buffer, size_t extra) {
    if (buffer == NULL) {
        return WC_EINVAL;
    }
    size_t need = 0;
    if (ckd_add(&need, buffer->len, extra) || ckd_add(&need, need, 1)) {
        return WC_EOVERFLOW;
    }
    if (need <= buffer->cap) {
        return WC_OK;
    }
    size_t cap = buffer->cap == 0 ? 64 : buffer->cap;
    while (cap < need) {
        size_t next = 0;
        if (ckd_mul(&next, cap, 2)) {
            cap = need;
            break;
        }
        cap = next;
    }
    unsigned char *data = wc_mem_realloc(buffer->data, cap);
    if (data == NULL) {
        return WC_ENOMEM;
    }
    buffer->data = data;
    buffer->cap = cap;
    return WC_OK;
}

WC_NODISCARD wc_status wc_buf_append(wc_buf *buffer, const void *data, size_t len) {
    if (buffer == NULL || (data == NULL && len != 0)) {
        return WC_EINVAL;
    }
    if (len == 0) {
        return WC_OK;
    }
    wc_status status = wc_buf_reserve(buffer, len);
    if (status != WC_OK) {
        return status;
    }
    memcpy(buffer->data + buffer->len, data, len);
    buffer->len += len;
    buffer->data[buffer->len] = '\0';
    return WC_OK;
}

WC_NODISCARD wc_status wc_buf_append_byte(wc_buf *buffer, unsigned char value) {
    return wc_buf_append(buffer, &value, 1);
}

WC_NODISCARD wc_status wc_buf_append_cstr(wc_buf *buffer, const char *text) {
    if (text == NULL) {
        return WC_EINVAL;
    }
    return wc_buf_append(buffer, text, strlen(text));
}

WC_NODISCARD wc_status wc_buf_appendf(wc_buf *buffer, const char *fmt, ...) {
    if (buffer == NULL || fmt == NULL) {
        return WC_EINVAL;
    }
    va_list args;
    va_start(args, fmt);
    va_list measure;
    va_copy(measure, args);
    int needed = vsnprintf(NULL, 0, fmt, measure);
    va_end(measure);
    if (needed < 0) {
        va_end(args);
        return WC_EINVAL;
    }
    size_t len = (size_t)needed;
    wc_status status = wc_buf_reserve(buffer, len);
    if (status != WC_OK) {
        va_end(args);
        return status;
    }
    int written = vsnprintf((char *)buffer->data + buffer->len, len + 1, fmt, args);
    va_end(args);
    if (written < 0) {
        return WC_EINVAL;
    }
    buffer->len += len;
    return WC_OK;
}

wc_str wc_buf_view(const wc_buf *buffer) {
    if (buffer == NULL || buffer->data == NULL) {
        return wc_str_from_parts(NULL, 0);
    }
    return wc_str_from_parts((const char *)buffer->data, buffer->len);
}

bool wc_buf_at(const wc_buf *buffer, size_t index, unsigned char *out) {
    if (buffer == NULL || out == NULL || index >= buffer->len) {
        return false;
    }
    *out = buffer->data[index];
    return true;
}
