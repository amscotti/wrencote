#ifndef WC_CORE_STR_H
#define WC_CORE_STR_H

#include <stdbool.h>
#include <stddef.h>

/* A borrowed byte view. Not NUL-terminated by contract. */
typedef struct wc_str {
    const char *data;
    size_t len;
} wc_str;

wc_str wc_str_from_cstr(const char *text);
wc_str wc_str_from_parts(const char *data, size_t len);

bool wc_str_empty(wc_str value);
bool wc_str_eq(wc_str left, wc_str right);
/* ASCII-only case folding; used for HTTP tokens. */
bool wc_str_eq_ci(wc_str left, wc_str right);
bool wc_str_starts_with(wc_str value, wc_str prefix);
bool wc_str_ends_with(wc_str value, wc_str suffix);

#endif
