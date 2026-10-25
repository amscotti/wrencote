#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "core/buf.h"

int main(void) {
    wc_buf buffer;
    wc_buf_init(&buffer);
    assert(buffer.len == 0 && buffer.cap == 0);

    assert(wc_buf_append_cstr(&buffer, "hello") == WC_OK);
    assert(buffer.len == 5);
    assert(strcmp((const char *)buffer.data, "hello") == 0);

    assert(wc_buf_append(&buffer, " world", 6) == WC_OK);
    assert(strcmp((const char *)buffer.data, "hello world") == 0);
    assert(wc_buf_append(&buffer, NULL, 0) == WC_OK);
    assert(wc_buf_append(&buffer, NULL, 1) == WC_EINVAL);

    assert(wc_buf_appendf(&buffer, " %d", 42) == WC_OK);
    assert(strcmp((const char *)buffer.data, "hello world 42") == 0);

    unsigned char byte = 0;
    assert(wc_buf_at(&buffer, 0, &byte));
    assert(byte == 'h');
    assert(!wc_buf_at(&buffer, buffer.len, &byte));
    assert(!wc_buf_at(NULL, 0, &byte));

    wc_str view = wc_buf_view(&buffer);
    assert(view.len == buffer.len);
    assert(memcmp(view.data, "hello world 42", view.len) == 0);

    size_t before = buffer.cap;
    wc_buf_clear(&buffer);
    assert(buffer.len == 0);
    assert(buffer.cap == before);
    assert(strcmp((const char *)buffer.data, "") == 0);

    assert(wc_buf_reserve(&buffer, SIZE_MAX) == WC_EOVERFLOW);

    char big[300];
    memset(big, 'x', sizeof big);
    big[sizeof big - 1] = '\0';
    assert(wc_buf_append_cstr(&buffer, big) == WC_OK);
    assert(buffer.len == sizeof big - 1);

    wc_buf_free(&buffer);
    assert(buffer.data == NULL && buffer.len == 0 && buffer.cap == 0);

    unsigned char byte2 = 0;
    wc_buf_init(NULL);
    wc_buf_free(NULL);
    wc_buf_clear(NULL);
    assert(wc_buf_append(NULL, NULL, 0) == WC_EINVAL);
    assert(wc_buf_append_cstr(NULL, "x") == WC_EINVAL);
    assert(wc_buf_appendf(NULL, "x") == WC_EINVAL);
    assert(wc_buf_appendf(&buffer, NULL) == WC_EINVAL);
    assert(!wc_buf_at(NULL, 0, &byte2));
    wc_str empty = wc_buf_view(NULL);
    assert(empty.len == 0 && empty.data == NULL);
    return 0;
}
