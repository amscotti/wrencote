#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "http/http.h"

static void assert_view(wc_str view, const char *text) {
    assert(view.len == strlen(text));
    assert(memcmp(view.data, text, view.len) == 0);
}

static void test_basic(void) {
    const char *raw = "GET /hello HTTP/1.1\r\nHost: example\r\n\r\n";
    wc_http_request request;
    int status = 0;
    assert(wc_http_parse_request(raw, strlen(raw), &request, &status) == WC_HTTP_PARSE_OK);
    assert_view(request.method, "GET");
    assert_view(request.path, "/hello");
    assert(request.query.len == 0);
    assert_view(request.version, "HTTP/1.1");
    assert(request.keep_alive);
    assert(!request.connection_close);
    assert(request.has_host);
    assert(request.content_length == 0);
    assert(request.head_len == strlen(raw));
    const wc_str *host = wc_http_request_header(&request, "HOST");
    assert(host != NULL);
    assert_view(*host, "example");
    assert(wc_http_request_header(&request, "missing") == NULL);
}

static void test_query_and_body(void) {
    const char *raw = "POST /a/b?x=1&y=2 HTTP/1.1\r\n"
                      "Host: x\r\n"
                      "Content-Length: 5\r\n"
                      "\r\n"
                      "hello";
    wc_http_request request;
    int status = 0;
    assert(wc_http_parse_request(raw, strlen(raw), &request, &status) == WC_HTTP_PARSE_OK);
    assert_view(request.path, "/a/b");
    assert_view(request.query, "x=1&y=2");
    assert(request.content_length == 5);
    assert(request.head_len == strlen(raw) - 5);
    assert(request.keep_alive);
}

static void test_connection_rules(void) {
    const char *ten = "GET / HTTP/1.0\r\n\r\n";
    wc_http_request request;
    int status = 0;
    assert(wc_http_parse_request(ten, strlen(ten), &request, &status) == WC_HTTP_PARSE_OK);
    assert(!request.keep_alive);

    const char *ten_keep = "GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n";
    assert(wc_http_parse_request(ten_keep, strlen(ten_keep), &request, &status) ==
           WC_HTTP_PARSE_OK);
    assert(request.keep_alive);

    const char *eleven_close = "GET / HTTP/1.1\r\nConnection: close\r\n\r\n";
    assert(wc_http_parse_request(eleven_close, strlen(eleven_close), &request, &status) ==
           WC_HTTP_PARSE_OK);
    assert(!request.keep_alive);
    assert(request.connection_close);
}

static void test_rejections(void) {
    wc_http_request request;
    int status = 0;

    const char *duplicate_length =
        "POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n";
    assert(wc_http_parse_request(duplicate_length, strlen(duplicate_length), &request, &status) ==
           WC_HTTP_PARSE_BAD);
    assert(status == 400);

    const char *transfer = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    assert(wc_http_parse_request(transfer, strlen(transfer), &request, &status) ==
           WC_HTTP_PARSE_BAD);

    const char *big_body = "POST / HTTP/1.1\r\nContent-Length: 2000000\r\n\r\n";
    assert(wc_http_parse_request(big_body, strlen(big_body), &request, &status) ==
           WC_HTTP_PARSE_BAD);
    assert(status == 413);

    const char *absolute = "GET http://x/ HTTP/1.1\r\n\r\n";
    assert(wc_http_parse_request(absolute, strlen(absolute), &request, &status) ==
           WC_HTTP_PARSE_BAD);

    const char *bad_version = "GET / HTTP/2.0\r\n\r\n";
    assert(wc_http_parse_request(bad_version, strlen(bad_version), &request, &status) ==
           WC_HTTP_PARSE_BAD);

    const char *folded = "GET / HTTP/1.1\r\nName: one\r\n two\r\n\r\n";
    assert(wc_http_parse_request(folded, strlen(folded), &request, &status) == WC_HTTP_PARSE_BAD);

    const char *bare_lf = "GET / HTTP/1.1\nHost: x\n\n";
    assert(wc_http_parse_request(bare_lf, strlen(bare_lf), &request, &status) ==
           WC_HTTP_PARSE_MORE);

    const char *incomplete = "GET / HTTP/1.1\r\nHost: x\r\n";
    assert(wc_http_parse_request(incomplete, strlen(incomplete), &request, &status) ==
           WC_HTTP_PARSE_MORE);

    assert(wc_http_parse_request(NULL, 0, &request, &status) == WC_HTTP_PARSE_BAD);
}

static void test_header_limits(void) {
    char raw[40 * 1024];
    size_t pos = 0;
    pos += (size_t)snprintf(raw + pos, sizeof raw - pos, "GET / HTTP/1.1\r\n");
    for (int i = 0; i < 100; i++) {
        pos += (size_t)snprintf(raw + pos, sizeof raw - pos, "X-%d: v\r\n", i);
    }
    pos += (size_t)snprintf(raw + pos, sizeof raw - pos, "\r\n");
    wc_http_request request;
    int status = 0;
    assert(pos < sizeof raw);
    assert(wc_http_parse_request(raw, pos, &request, &status) == WC_HTTP_PARSE_OK);
    assert(request.header_count == 100);

    pos = (size_t)snprintf(raw, sizeof raw, "GET / HTTP/1.1\r\n");
    for (int i = 0; i < 101; i++) {
        pos += (size_t)snprintf(raw + pos, sizeof raw - pos, "X-%d: v\r\n", i);
    }
    pos += (size_t)snprintf(raw + pos, sizeof raw - pos, "\r\n");
    assert(wc_http_parse_request(raw, pos, &request, &status) == WC_HTTP_PARSE_BAD);
    assert(status == 431);
}

static void test_oversized_head(void) {
    static char raw[36 * 1024];
    size_t pos = 0;
    pos += (size_t)snprintf(raw + pos, sizeof raw - pos, "GET / HTTP/1.1\r\nX: ");
    memset(raw + pos, 'a', 33 * 1024);
    pos += 33 * 1024;
    pos += (size_t)snprintf(raw + pos, sizeof raw - pos, "\r\n\r\n");
    wc_http_request request;
    int status = 0;
    assert(wc_http_parse_request(raw, pos, &request, &status) == WC_HTTP_PARSE_BAD);
    assert(status == 431);
}

int main(void) {
    test_basic();
    test_query_and_body();
    test_connection_rules();
    test_rejections();
    test_header_limits();
    test_oversized_head();
    return 0;
}
