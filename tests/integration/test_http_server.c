#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "wrencote/wrencote.h"

static void log_sink(void *user, int level, const char *line, size_t len) {
    (void)user;
    (void)level;
    fprintf(stderr, "log: %.*s\n", (int)len, line);
}

static void *run_host(void *user) {
    wc_host *host = user;
    (void)wc_host_run(host);
    return NULL;
}

static int connect_with_rcvbuf(uint16_t port, int rcvbuf) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    if (rcvbuf > 0) {
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
    }
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    assert(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(connect(fd, (struct sockaddr *)&address, sizeof address) == 0);
    return fd;
}

static int connect_to(uint16_t port) {
    return connect_with_rcvbuf(port, 0);
}

static void send_all(int fd, const char *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, 0);
        assert(n > 0);
        sent += (size_t)n;
    }
}

typedef struct response {
    int status;
    size_t body_len;
    char body[4096];
    size_t header_len;
} response;

/* Buffered reader: retains bytes past the current response so coalesced
 * pipelined responses are not discarded (a fresh read per response is racy). */
typedef struct reader {
    int fd;
    char buffer[65536];
    size_t begin;
    size_t end;
} reader;

static void reader_init(reader *r, int fd) {
    memset(r, 0, sizeof *r);
    r->fd = fd;
}

static void reader_compact(reader *r) {
    if (r->begin > 0) {
        memmove(r->buffer, r->buffer + r->begin, r->end - r->begin);
        r->end -= r->begin;
        r->begin = 0;
    }
}

static size_t reader_fill(reader *r) {
    reader_compact(r);
    assert(r->end < sizeof r->buffer);
    ssize_t n = recv(r->fd, r->buffer + r->end, sizeof r->buffer - r->end, 0);
    assert(n > 0);
    r->end += (size_t)n;
    return (size_t)n;
}

static char *find_bytes(char *haystack, size_t hay_len, const char *needle, size_t needle_len) {
    if (needle_len == 0 || needle_len > hay_len) {
        return NULL;
    }
    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return NULL;
}

static response reader_response(reader *r) {
    response result;
    memset(&result, 0, sizeof result);
    char *header_end = NULL;
    while ((header_end = find_bytes(r->buffer + r->begin, r->end - r->begin, "\r\n\r\n", 4)) ==
           NULL) {
        reader_fill(r);
    }
    size_t header_len = (size_t)(header_end - (r->buffer + r->begin)) + 4;
    result.header_len = header_len;
    assert(sscanf(r->buffer + r->begin, "HTTP/1.1 %d", &result.status) == 1);
    char header[8192];
    assert(header_len < sizeof header);
    memcpy(header, r->buffer + r->begin, header_len);
    header[header_len] = '\0';
    const char *length_header = strstr(header, "Content-Length: ");
    size_t content_length =
        length_header != NULL ? (size_t)strtoul(length_header + 16, NULL, 10) : 0;
    assert(content_length < sizeof result.body);
    while (r->end - r->begin - header_len < content_length) {
        reader_fill(r);
    }
    result.body_len = content_length;
    memcpy(result.body, r->buffer + r->begin + header_len, content_length);
    result.body[content_length] = '\0';
    r->begin += header_len + content_length;
    if (r->begin == r->end) {
        r->begin = 0;
        r->end = 0;
    }
    return result;
}

static response read_response(int fd) {
    reader r;
    reader_init(&r, fd);
    return reader_response(&r);
}

static response request(uint16_t port, const char *raw) {
    int fd = connect_to(port);
    send_all(fd, raw, strlen(raw));
    response result = read_response(fd);
    close(fd);
    return result;
}

static void test_slowloris(uint16_t port) {
    int fd = connect_to(port);
    struct timeval timeout = {15, 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    const char *partial = "GET /health HTTP/1.1\r\nHost: local\r\n";
    send_all(fd, partial, strlen(partial));
    response result = read_response(fd);
    assert(result.status == 408);
    close(fd);
}

static void test_keepalive(uint16_t port, int count) {
    int fd = connect_to(port);
    for (int i = 0; i < count; i++) {
        const char *raw = "GET /health HTTP/1.1\r\nHost: local\r\n\r\n";
        send_all(fd, raw, strlen(raw));
        response result = read_response(fd);
        assert(result.status == 200);
        assert(strcmp(result.body, "ok\n") == 0);
    }
    close(fd);
}

/* Two requests sent in one write must both be answered without waiting for
 * another read event (regression: pipelined requests stalled into a 408).
 * Responses are commonly coalesced in one TCP segment, so read through a
 * buffered reader that retains the surplus. */
static void test_pipelining(uint16_t port) {
    int fd = connect_to(port);
    struct timeval timeout = {10, 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    const char *two = "GET /health HTTP/1.1\r\nHost: local\r\n\r\n"
                      "GET /nope HTTP/1.1\r\nHost: local\r\n\r\n";
    send_all(fd, two, strlen(two));
    reader r;
    reader_init(&r, fd);
    response first = reader_response(&r);
    assert(first.status == 200);
    assert(strcmp(first.body, "ok\n") == 0);
    response second = reader_response(&r);
    assert(second.status == 404);
    assert(strcmp(second.body, "not found\n") == 0);
    close(fd);
}

/* A response larger than the kernel's send buffer must complete once the
 * client drains it (regression: write interest was never registered, so the
 * response stalled after the first EAGAIN). */
static void test_backpressure(uint16_t port) {
    int fd = connect_with_rcvbuf(port, 64 * 1024);
    struct timeval timeout = {20, 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    const char *raw = "GET /huge HTTP/1.1\r\nHost: local\r\n\r\n";
    send_all(fd, raw, strlen(raw));
    /* Let the server fill the socket pair and hit EAGAIN while we read
     * nothing. */
    const struct timespec pause = {0, 200 * 1000 * 1000};
    nanosleep(&pause, NULL);
    size_t content_length = 8u * 1024u * 1024u;
    size_t cap = content_length + 65536;
    char *data = malloc(cap);
    assert(data != NULL);
    size_t used = 0;
    char *header_end = NULL;
    size_t header_len = 0;
    while (used < cap - 1) {
        ssize_t n = recv(fd, data + used, cap - 1 - used, 0);
        assert(n > 0);
        used += (size_t)n;
        data[used] = '\0';
        if (header_end == NULL) {
            header_end = strstr(data, "\r\n\r\n");
            if (header_end != NULL) {
                header_len = (size_t)(header_end - data) + 4;
                const char *length_header = strstr(data, "Content-Length: ");
                assert(length_header != NULL);
                size_t declared = (size_t)strtoul(length_header + 16, NULL, 10);
                assert(declared == content_length);
            }
        }
        if (header_end != NULL && used - header_len >= content_length) {
            break;
        }
    }
    assert(header_end != NULL);
    assert(used - header_len == content_length);
    for (size_t i = header_len; i < used; i++) {
        assert(data[i] == 'x');
    }
    free(data);
    close(fd);
}

int main(void) {
    wc_host_config config;
    wc_host_config_init(&config);
    config.vm_heap_bytes = 64 * 1024 * 1024;
    config.max_response_bytes = 16 * 1024 * 1024;
    config.log = log_sink;
    config.capabilities = WC_CAP_NET;
    wc_host *host = NULL;
    assert(wc_host_new(&config, &host) == WC_OK);
    assert(wc_host_serve(host, "127.0.0.1", 0) == WC_OK);
    uint16_t port = wc_host_local_port(host);
    assert(port != 0);
    assert(wc_host_load(host, "tests/data/http_app.wren") == WC_OK);

    pthread_t thread;
    assert(pthread_create(&thread, NULL, run_host, host) == 0);

    response health = request(port, "GET /health HTTP/1.1\r\nHost: local\r\n\r\n");
    assert(health.status == 200);
    assert(strcmp(health.body, "ok\n") == 0);

    response missing = request(port, "GET /nope HTTP/1.1\r\nHost: local\r\n\r\n");
    assert(missing.status == 404);
    assert(strcmp(missing.body, "not found\n") == 0);

    response echo =
        request(port, "POST /echo HTTP/1.1\r\nHost: local\r\nContent-Length: 5\r\n\r\nhello");
    assert(echo.status == 200);
    assert(strcmp(echo.body, "hello") == 0);

    response header = request(port, "GET /headers HTTP/1.1\r\nHost: local\r\nX-Probe: abc\r\n\r\n");
    assert(header.status == 200);
    assert(strcmp(header.body, "abc") == 0);

    test_keepalive(port, 100);
    test_pipelining(port);
    test_backpressure(port);
    test_slowloris(port);

    response bad = request(port, "GET / HTTP/1.1\r\nBroken\r\n\r\n");
    assert(bad.status == 400);

    response huge =
        request(port, "POST /echo HTTP/1.1\r\nHost: local\r\nContent-Length: 2000000\r\n\r\n");
    assert(huge.status == 413);

    char oversized[4096];
    size_t pos = (size_t)snprintf(oversized, sizeof oversized, "GET / HTTP/1.1\r\nHost: local\r\n");
    for (int i = 0; i < 101; i++) {
        pos += (size_t)snprintf(oversized + pos, sizeof oversized - pos, "X-%d: v\r\n", i);
    }
    pos += (size_t)snprintf(oversized + pos, sizeof oversized - pos, "\r\n");
    response many_headers = request(port, oversized);
    assert(many_headers.status == 431);

    response inspect = request(
        port, "GET /inspect?a=1&b=two+words HTTP/1.1\r\nHost: local\r\nX-Probe: abc\r\n\r\n");
    assert(inspect.status == 200);
    assert(strcmp(inspect.body, "GET|1.1|false|1|two words|abc") == 0);

    response created = request(port, "GET /created HTTP/1.1\r\nHost: local\r\n\r\n");
    assert(created.status == 201 && strcmp(created.body, "made") == 0);

    response no_content = request(port, "GET /no-content HTTP/1.1\r\nHost: local\r\n\r\n");
    assert(no_content.status == 204 && no_content.body_len == 0);

    response running = request(port, "GET /running HTTP/1.1\r\nHost: local\r\n\r\n");
    assert(running.status == 200 && strcmp(running.body, "true") == 0);

    response stopped = request(port, "GET /stop HTTP/1.1\r\nHost: local\r\n\r\n");
    assert(stopped.status == 200 && strcmp(stopped.body, "bye") == 0);

    pthread_join(thread, NULL);
    wc_host_stop(host);
    wc_host_free(host);
    return 0;
}
