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

static int connect_to(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    assert(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(connect(fd, (struct sockaddr *)&address, sizeof address) == 0);
    return fd;
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

static response read_response(int fd) {
    response result;
    memset(&result, 0, sizeof result);
    char buffer[8192];
    size_t used = 0;
    char *header_end = NULL;
    while (used < sizeof buffer) {
        ssize_t n = recv(fd, buffer + used, sizeof buffer - used, 0);
        assert(n > 0);
        used += (size_t)n;
        buffer[used] = '\0';
        header_end = strstr(buffer, "\r\n\r\n");
        if (header_end != NULL) {
            break;
        }
    }
    assert(header_end != NULL);
    result.header_len = (size_t)(header_end - buffer) + 4;
    assert(sscanf(buffer, "HTTP/1.1 %d", &result.status) == 1);
    const char *length_header = strstr(buffer, "Content-Length: ");
    assert(length_header != NULL);
    size_t content_length = (size_t)strtoul(length_header + 16, NULL, 10);
    size_t have = used - result.header_len;
    while (have < content_length) {
        ssize_t n = recv(fd, buffer + result.header_len + have, content_length - have, 0);
        assert(n > 0);
        have += (size_t)n;
    }
    result.body_len = content_length;
    assert(content_length < sizeof result.body);
    memcpy(result.body, buffer + result.header_len, content_length);
    result.body[content_length] = '\0';
    return result;
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

int main(void) {
    wc_host_config config;
    wc_host_config_init(&config);
    config.vm_heap_bytes = 4 * 1024 * 1024;
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
