#define _GNU_SOURCE

#include <arpa/inet.h>
#include <assert.h>
#include <dirent.h>
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
    int served;
    char buffer[4096];
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

static long read_rss_kb(int pid) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/status", pid);
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    char line[256];
    long value = -1;
    while (fgets(line, sizeof line, file) != NULL) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            (void)sscanf(line + 6, "%ld", &value);
            break;
        }
    }
    fclose(file);
    return value;
}

static int read_fd_count(int pid) {
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/fd", pid);
    int count = 0;
    DIR *dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '.') {
            count++;
        }
    }
    closedir(dir);
    return count;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <port> <server-pid> [seconds] [rate] [connections]\n", argv[0]);
        return 2;
    }
    uint16_t port = (uint16_t)atoi(argv[1]);
    int server_pid = atoi(argv[2]);
    int seconds = argc > 3 ? atoi(argv[3]) : 60;
    int rate = argc > 4 ? atoi(argv[4]) : 5000;
    int connections = argc > 5 ? atoi(argv[5]) : 512;
    int total = seconds * rate;
    int inflight_target = 8;

    conn *conns = calloc((size_t)connections, sizeof *conns);
    assert(conns != NULL);
    int epoll = epoll_create1(EPOLL_CLOEXEC);
    assert(epoll >= 0);
    for (int i = 0; i < connections; i++) {
        conns[i].fd = connect_to(port);
        conns[i].idle = true;
        struct epoll_event event = {.events = EPOLLIN, .data.u32 = (uint32_t)i};
        assert(epoll_ctl(epoll, EPOLL_CTL_ADD, conns[i].fd, &event) == 0);
    }

    static const char request[] = "GET / HTTP/1.1\r\nHost: soak\r\n\r\n";
    int sent = 0;
    int completed = 0;
    int inflight = 0;
    int next = 0;
    double next_send = now_seconds();
    long rss_start = -1;
    int fd_start = -1;

    while (completed < total) {
        double now = now_seconds();
        while (inflight < inflight_target && sent < total && now >= next_send) {
            int picked = -1;
            for (int k = 0; k < connections; k++) {
                int i = (next + k) % connections;
                if (conns[i].idle) {
                    picked = i;
                    break;
                }
            }
            if (picked < 0) {
                break;
            }
            conns[picked].used = 0;
            ssize_t n = send(conns[picked].fd, request, sizeof request - 1, 0);
            assert(n == (ssize_t)(sizeof request - 1));
            conns[picked].idle = false;
            next = (picked + 1) % connections;
            inflight++;
            sent++;
            next_send += 1.0 / (double)rate;
        }
        if (rss_start < 0 && sent > rate * 300) {
            rss_start = read_rss_kb(server_pid);
            fd_start = read_fd_count(server_pid);
        }
        now = now_seconds();
        int timeout = 100;
        if (sent < total) {
            double wait = (next_send - now) * 1000.0;
            timeout = wait < 0 ? 0 : wait > 100 ? 100 : (int)wait;
        }
        struct epoll_event events[64];
        int ready = epoll_wait(epoll, events, 64, timeout);
        assert(ready >= 0);
        for (int e = 0; e < ready; e++) {
            uint32_t index = events[e].data.u32;
            conn *c = &conns[index];
            if ((events[e].events & (EPOLLERR | EPOLLHUP)) != 0) {
                fprintf(stderr, "connection %u closed at %d/%d\n", index, completed, total);
                return 1;
            }
            for (;;) {
                ssize_t n = recv(c->fd, c->buffer + c->used, sizeof c->buffer - c->used, 0);
                if (n > 0) {
                    c->used += (size_t)n;
                } else if (n == 0) {
                    fprintf(stderr, "connection %u EOF at %d/%d\n", index, completed, total);
                    return 1;
                } else if (errno == EAGAIN || errno == EINTR) {
                    break;
                } else {
                    return 1;
                }
                if (response_complete(c)) {
                    completed++;
                    c->served++;
                    c->idle = true;
                    inflight--;
                    c->used = 0;
                    if (c->served >= 900) {
                        close(c->fd);
                        c->fd = connect_to(port);
                        c->served = 0;
                        struct epoll_event event = {.events = EPOLLIN,
                                                    .data.u32 = index};
                        assert(epoll_ctl(epoll, EPOLL_CTL_ADD, c->fd, &event) == 0);
                    }
                    break;
                }
            }
        }
    }

    long rss_end = read_rss_kb(server_pid);
    int fd_end = read_fd_count(server_pid);
    double growth = rss_start > 0 ? 100.0 * (double)(rss_end - rss_start) / (double)rss_start : 0.0;
    printf("seconds=%d rate=%d requests=%d rss_start=%ldkB rss_end=%ldkB growth=%.2f%% "
           "fds_start=%d fds_end=%d\n",
           seconds, rate, total, rss_start, rss_end, growth, fd_start, fd_end);
    return (growth <= 2.0 && fd_end == fd_start) ? 0 : 1;
}
