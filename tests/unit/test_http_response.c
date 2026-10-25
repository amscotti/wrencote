#define _GNU_SOURCE

#include <assert.h>
#include <string.h>

#include "http/internal.h"

static bool contains(const wc_buf *buffer, const char *text) {
    size_t length = strlen(text);
    return length <= buffer->len && memmem(buffer->data, buffer->len, text, length) != NULL;
}

static wc_http_response make_response(wc_http_conn *conn) {
    wc_http_response response;
    memset(&response, 0, sizeof response);
    response.conn = conn;
    wc_buf_init(&response.headers);
    return response;
}

int main(void) {
    wc_http_server server;
    memset(&server, 0, sizeof server);
    server.config.max_response_bytes = 16;
    wc_http_conn conn;
    memset(&conn, 0, sizeof conn);
    conn.server = &server;
    conn.request.method = (wc_str){"GET", 3};
    wc_buf_init(&conn.out);
    wc_http_response response = make_response(&conn);

    assert(wc_http_response_set_header(&response, "X-Test", 6, "one", 3) == WC_OK);
    assert(wc_http_response_send(&response, 201, "made", 4) == WC_OK);
    assert(wc_http_response_sent(&response));
    assert(contains(&conn.out, "HTTP/1.1 201 Created\r\n"));
    assert(contains(&conn.out, "x-test: one\r\n"));
    assert(contains(&conn.out, "Content-Length: 4\r\n"));
    assert(contains(&conn.out, "Connection: keep-alive\r\n"));
    assert(conn.out.data[conn.out.len - 4] == 'm');

    assert(wc_http_response_send(&response, 201, "x", 1) == WC_EINVAL);
    assert(wc_http_response_set_header(&response, "X", 1, "y", 1) == WC_EINVAL);

    wc_http_response_reset(&response);
    wc_buf_clear(&conn.out);
    conn.closing = true;
    assert(wc_http_response_send(&response, 204, "", 0) == WC_OK);
    assert(contains(&conn.out, "204 No Content\r\n"));
    assert(contains(&conn.out, "Content-Length: 0\r\n"));
    assert(contains(&conn.out, "Connection: close\r\n"));
    assert(!contains(&conn.out, "\r\n\r\nerror"));

    wc_http_response_reset(&response);
    wc_buf_clear(&conn.out);
    conn.closing = false;
    conn.request.method = (wc_str){"HEAD", 4};
    assert(wc_http_response_send(&response, 200, "abc", 3) == WC_OK);
    assert(contains(&conn.out, "Content-Length: 3\r\n"));
    assert(!contains(&conn.out, "\r\n\r\nabc"));

    wc_http_response_reset(&response);
    wc_buf_clear(&conn.out);
    conn.request.method = (wc_str){"GET", 3};
    assert(wc_http_response_send(&response, 99, "x", 1) == WC_EINVAL);
    assert(wc_http_response_send(&response, 500, "0123456789abcdefg", 17) == WC_EOVERFLOW);

    wc_http_response_reset(&response);
    wc_buf_clear(&conn.out);
    assert(wc_http_response_set_header(&response, "Content-Length", 14, "1", 1) == WC_EINVAL);
    assert(wc_http_response_set_header(&response, "Bad Name", 8, "1", 1) == WC_EINVAL);
    assert(wc_http_response_set_header(&response, "X", 1, "bad\r\n", 5) == WC_EINVAL);
    assert(wc_http_response_set_header(NULL, "X", 1, "y", 1) == WC_EINVAL);

    for (int i = 0; i < 64; i++) {
        assert(wc_http_response_set_header(&response, "X", 1, "y", 1) == WC_OK);
    }
    assert(wc_http_response_set_header(&response, "X", 1, "y", 1) == WC_EINVAL);

    wc_buf_free(&conn.out);
    wc_buf_free(&response.headers);
    return 0;
}
