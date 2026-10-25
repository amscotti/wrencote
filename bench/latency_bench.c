#define _GNU_SOURCE

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct conn {
    int fd;
    bool idle;
    size_t used;
    char buffer[4096];
    double started;
} conn;

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int connect_to(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);
    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    assert(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(connect(fd, (struct sockaddr *)&address, sizeof address) == 0);
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
    return fd;
}

static bool response_complete(conn *c) {
    char *end = memmem(c->buffer, c->used, "\r\n\r\n", 4);
    if (end == NULL) {
        return false;
    }
    size_t header_length = (size_t)(end - c->buffer) + 4;
    char *length_header = memmem(c->buffer, header_length, "Content-Length: ", 16);
    if (length_header == NULL) {
        return false;
    }
    size_t body_length = (size_t)strtoul(length_header + 16, NULL, 10);
    return c->used - header_length >= body_length;
}

static int compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <port> [connections] [inflight] [requests]\n", argv[0]);
        return 2;
    }
    uint16_t port = (uint16_t)atoi(argv[1]);
    int connections = argc > 2 ? atoi(argv[2]) : 512;
    int inflight_target = argc > 3 ? atoi(argv[3]) : 16;
    int total = argc > 4 ? atoi(argv[4]) : 200000;
    if (connections < 1 || inflight_target < 1 || inflight_target > connections || total < 1) {
        return 2;
    }

    conn *conns = calloc((size_t)connections, sizeof *conns);
    double *latencies = calloc((size_t)total, sizeof *latencies);
    assert(conns != NULL && latencies != NULL);
    int epoll = epoll_create1(EPOLL_CLOEXEC);
    assert(epoll >= 0);
    for (int i = 0; i < connections; i++) {
        conns[i].fd = connect_to(port);
        conns[i].idle = true;
        struct epoll_event event = {.events = EPOLLIN, .data.u32 = (uint32_t)i};
        assert(epoll_ctl(epoll, EPOLL_CTL_ADD, conns[i].fd, &event) == 0);
    }

    static const char request[] = "GET / HTTP/1.1\r\nHost: lat\r\n\r\n";
    int sent = 0;
    int completed = 0;
    int inflight = 0;
    int next = 0;
    double start = now_seconds();
    while (completed < total) {
        for (int k = 0; k < connections && inflight < inflight_target && sent < total; k++) {
            int i = (next + k) % connections;
            if (!conns[i].idle) {
                continue;
            }
            next = (i + 1) % connections;
            conns[i].used = 0;
            conns[i].started = now_seconds();
            ssize_t n = send(conns[i].fd, request, sizeof request - 1, 0);
            assert(n == (ssize_t)(sizeof request - 1));
            conns[i].idle = false;
            inflight++;
            sent++;
        }
        struct epoll_event events[64];
        int ready = epoll_wait(epoll, events, 64, 100);
        assert(ready >= 0);
        for (int e = 0; e < ready; e++) {
            uint32_t index = events[e].data.u32;
            conn *c = &conns[index];
            if ((events[e].events & (EPOLLERR | EPOLLHUP)) != 0) {
                fprintf(stderr, "connection %u closed\n", index);
                return 1;
            }
            for (;;) {
                ssize_t n = recv(c->fd, c->buffer + c->used, sizeof c->buffer - c->used, 0);
                if (n > 0) {
                    c->used += (size_t)n;
                } else if (n == 0) {
                    fprintf(stderr, "connection %u EOF\n", index);
                    return 1;
                } else if (errno == EAGAIN || errno == EINTR) {
                    break;
                } else {
                    return 1;
                }
                if (response_complete(c)) {
                    latencies[completed++] = now_seconds() - c->started;
                    c->idle = true;
                    inflight--;
                    c->used = 0;
                    break;
                }
            }
        }
    }
    double elapsed = now_seconds() - start;
    qsort(latencies, (size_t)total, sizeof *latencies, compare_double);
    printf("connections=%d inflight=%d requests=%d seconds=%.3f req_per_sec=%.0f "
           "p50_ms=%.3f p99_ms=%.3f max_ms=%.3f\n",
           connections, inflight_target, total, elapsed, (double)total / elapsed,
           latencies[total / 2] * 1e3, latencies[(total * 99) / 100] * 1e3,
           latencies[total - 1] * 1e3);
    return 0;
}
