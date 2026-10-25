#include "core/str.h"

#include <string.h>

static unsigned char ascii_lower(unsigned char value) {
    if (value >= 'A' && value <= 'Z') {
        return (unsigned char)(value + ('a' - 'A'));
    }
    return value;
}

wc_str wc_str_from_cstr(const char *text) {
    wc_str value = {NULL, 0};
    if (text != NULL) {
        value.data = text;
        value.len = strlen(text);
    }
    return value;
}

wc_str wc_str_from_parts(const char *data, size_t len) {
    wc_str value = {data, len};
    return value;
}

bool wc_str_empty(wc_str value) {
    return value.len == 0;
}

bool wc_str_eq(wc_str left, wc_str right) {
    if (left.len != right.len) {
        return false;
    }
    if (left.len == 0) {
        return true;
    }
    if (left.data == NULL || right.data == NULL) {
        return false;
    }
    return memcmp(left.data, right.data, left.len) == 0;
}

bool wc_str_eq_ci(wc_str left, wc_str right) {
    if (left.len != right.len) {
        return false;
    }
    if (left.len == 0) {
        return true;
    }
    if (left.data == NULL || right.data == NULL) {
        return false;
    }
    for (size_t i = 0; i < left.len; i++) {
        if (ascii_lower((unsigned char)left.data[i]) != ascii_lower((unsigned char)right.data[i])) {
            return false;
        }
    }
    return true;
}

bool wc_str_starts_with(wc_str value, wc_str prefix) {
    if (prefix.len > value.len) {
        return false;
    }
    if (prefix.len == 0) {
        return true;
    }
    if (value.data == NULL || prefix.data == NULL) {
        return false;
    }
    return memcmp(value.data, prefix.data, prefix.len) == 0;
}

bool wc_str_ends_with(wc_str value, wc_str suffix) {
    if (suffix.len > value.len) {
        return false;
    }
    if (suffix.len == 0) {
        return true;
    }
    if (value.data == NULL || suffix.data == NULL) {
        return false;
    }
    return memcmp(value.data + (value.len - suffix.len), suffix.data, suffix.len) == 0;
}
