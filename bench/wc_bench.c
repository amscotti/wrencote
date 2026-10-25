#define _GNU_SOURCE

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "wrencote/wrencote.h"

typedef struct client {
    uint16_t port;
    int requests;
    pthread_barrier_t *barrier;
    int errors;
    double *latencies;
} client;

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
    if (connect(fd, (struct sockaddr *)&address, sizeof address) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static bool read_one(int fd, char *buffer, size_t capacity) {
    size_t used = 0;
    char *header_end = NULL;
    while (used < capacity) {
        ssize_t n = recv(fd, buffer + used, capacity - used, 0);
        if (n <= 0) {
            return false;
        }
        used += (size_t)n;
        header_end = memmem(buffer, used, "\r\n\r\n", 4);
        if (header_end != NULL) {
            break;
        }
    }
    if (header_end == NULL) {
        return false;
    }
    size_t header_length = (size_t)(header_end - buffer) + 4;
    char *length_header = memmem(buffer, header_length, "Content-Length: ", 16);
    if (length_header == NULL) {
        return false;
    }
    size_t body_length = (size_t)strtoul(length_header + 16, NULL, 10);
    while (used - header_length < body_length) {
        ssize_t n = recv(fd, buffer + used, capacity - used, 0);
        if (n <= 0) {
            return false;
        }
        used += (size_t)n;
    }
    return true;
}

static void *client_run(void *user) {
    client *c = user;
    int fd = connect_to(c->port);
    if (fd < 0) {
        c->errors = c->requests;
        return NULL;
    }
    const char request[] = "GET / HTTP/1.1\r\nHost: bench\r\n\r\n";
    char buffer[4096];
    pthread_barrier_wait(c->barrier);
    for (int i = 0; i < c->requests; i++) {
        double request_start = now_seconds();
        size_t sent = 0;
        while (sent < sizeof request - 1) {
            ssize_t n = send(fd, request + sent, sizeof request - 1 - sent, 0);
            if (n <= 0) {
                c->errors++;
                close(fd);
                return NULL;
            }
            sent += (size_t)n;
        }
        if (!read_one(fd, buffer, sizeof buffer)) {
            c->errors++;
            close(fd);
            return NULL;
        }
        c->latencies[i] = now_seconds() - request_start;
    }
    close(fd);
    return NULL;
}

static void *run_host(void *user) {
    wc_host *host = user;
    (void)wc_host_run(host);
    return NULL;
}

static int compare_double(const void *left, const void *right) {
    double a = *(const double *)left;
    double b = *(const double *)right;
    return a < b ? -1 : a > b ? 1 : 0;
}

int main(int argc, char **argv) {
    int connections = argc > 1 ? atoi(argv[1]) : 4;
    int per_client = argc > 2 ? atoi(argv[2]) : 1000;
    if (connections < 1 || connections > 1024 || per_client < 1) {
        fprintf(stderr, "usage: %s [connections] [requests-per-connection]\n", argv[0]);
        return 2;
    }

    wc_host_config config;
    wc_host_config_init(&config);
    config.capabilities = WC_CAP_NET;
    config.vm_heap_bytes = 8 * 1024 * 1024;
    wc_host *host = NULL;
    if (wc_host_new(&config, &host) != WC_OK || wc_host_serve(host, "127.0.0.1", 0) != WC_OK) {
        fprintf(stderr, "host setup failed\n");
        return 1;
    }
    uint16_t port = wc_host_local_port(host);
    if (wc_host_load(host, "tests/data/bench_app.wren") != WC_OK) {
        fprintf(stderr, "script load failed\n");
        return 1;
    }

    pthread_t host_thread;
    assert(pthread_create(&host_thread, NULL, run_host, host) == 0);

    pthread_barrier_t barrier;
    assert(pthread_barrier_init(&barrier, NULL, (unsigned)connections + 1) == 0);
    client *clients = calloc((size_t)connections, sizeof *clients);
    pthread_t *threads = calloc((size_t)connections, sizeof *threads);
    assert(clients != NULL && threads != NULL);
    for (int i = 0; i < connections; i++) {
        clients[i] = (client){.port = port,
                              .requests = per_client,
                              .barrier = &barrier,
                              .latencies = calloc((size_t)per_client, sizeof(double))};
        assert(clients[i].latencies != NULL);
        assert(pthread_create(&threads[i], NULL, client_run, &clients[i]) == 0);
    }

    double start = now_seconds();
    pthread_barrier_wait(&barrier);
    int errors = 0;
    for (int i = 0; i < connections; i++) {
        pthread_join(threads[i], NULL);
        errors += clients[i].errors;
    }
    double elapsed = now_seconds() - start;
    long total = (long)connections * per_client;
    double *all = calloc((size_t)total, sizeof(double));
    size_t samples = 0;
    for (int i = 0; i < connections; i++) {
        for (int j = 0; j < per_client; j++) {
            all[samples++] = clients[i].latencies[j];
        }
    }
    qsort(all, samples, sizeof(double), compare_double);
    printf("connections=%d requests=%ld seconds=%.3f req_per_sec=%.0f us_per_req=%.2f "
           "p50_ms=%.3f p99_ms=%.3f max_ms=%.3f errors=%d\n",
           connections, total, elapsed, (double)total / elapsed, elapsed * 1e6 / (double)total,
           all[samples / 2] * 1e3, all[(samples * 99) / 100] * 1e3, all[samples - 1] * 1e3, errors);
    free(all);

    wc_host_stop(host);
    pthread_join(host_thread, NULL);
    wc_host_free(host);
    for (int i = 0; i < connections; i++) {
        free(clients[i].latencies);
    }
    free(clients);
    free(threads);
    pthread_barrier_destroy(&barrier);
    return errors == 0 ? 0 : 1;
}
