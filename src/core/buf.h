#ifndef WC_CORE_BUF_H
#define WC_CORE_BUF_H

#include <stdbool.h>
#include <stddef.h>

#include "core/compiler.h"
#include "core/status.h"
#include "core/str.h"

/* An owned byte buffer. Growth is geometric; data is always NUL-terminated
 * when len < cap so appendf/append_cstr callers can treat it as text. */
typedef struct wc_buf {
    unsigned char *data;
    size_t len;
    size_t cap;
} wc_buf;

void wc_buf_init(wc_buf *buffer);
void wc_buf_free(wc_buf *buffer);
void wc_buf_clear(wc_buf *buffer);

/* Ensures room for [extra] more bytes plus a NUL. */
WC_NODISCARD wc_status wc_buf_reserve(wc_buf *buffer, size_t extra);
WC_NODISCARD wc_status wc_buf_append(wc_buf *buffer, const void *data, size_t len);
WC_NODISCARD wc_status wc_buf_append_byte(wc_buf *buffer, unsigned char value);
WC_NODISCARD wc_status wc_buf_append_cstr(wc_buf *buffer, const char *text);
WC_NODISCARD wc_status wc_buf_appendf(wc_buf *buffer, const char *fmt, ...) WC_PRINTF(2, 3);

wc_str wc_buf_view(const wc_buf *buffer);
bool wc_buf_at(const wc_buf *buffer, size_t index, unsigned char *out);

#endif
