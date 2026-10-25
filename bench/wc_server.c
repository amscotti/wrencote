#include <stdio.h>
#include <stdlib.h>

#include "wrencote/wrencote.h"

int main(int argc, char **argv) {
    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : 8080;
    const char *script = argc > 2 ? argv[2] : "tests/data/bench_app.wren";
    wc_host_config config;
    wc_host_config_init(&config);
    config.capabilities = WC_CAP_NET;
    config.vm_heap_bytes = 8 * 1024 * 1024;
    wc_host *host = NULL;
    if (wc_host_new(&config, &host) != WC_OK) {
        fprintf(stderr, "host_new failed\n");
        return 1;
    }
    if (wc_host_serve(host, "127.0.0.1", port) != WC_OK) {
        fprintf(stderr, "serve failed\n");
        return 1;
    }
    if (wc_host_load(host, script) != WC_OK) {
        fprintf(stderr, "load failed\n");
        return 1;
    }
    printf("listening on %u\n", (unsigned)wc_host_local_port(host));
    fflush(stdout);
    wc_status status = wc_host_run(host);
    wc_host_free(host);
    return status == WC_OK ? 0 : 1;
}
