#include <stdio.h>
#include <stdlib.h>

#include "wrencote/wrencote.h"

static void log_sink(void *user, int level, const char *line, size_t len) {
    (void)user;
    (void)level;
    fprintf(stderr, "%.*s\n", (int)len, line);
}

int main(int argc, char **argv) {
    const char *script = argc > 1 ? argv[1] : "examples/app.wren";
    wc_host_config config;
    wc_host_config_init(&config);
    config.capabilities = WC_CAP_NET;
    config.log = log_sink;
    wc_host *host = NULL;
    if (wc_host_new(&config, &host) != WC_OK) {
        fprintf(stderr, "host_new failed\n");
        return 1;
    }
    if (wc_host_serve(host, "127.0.0.1", 18090) != WC_OK) {
        fprintf(stderr, "serve failed\n");
        wc_host_free(host);
        return 1;
    }
    if (wc_host_load(host, script) != WC_OK) {
        fprintf(stderr, "load failed: %s\n", wc_host_last_error(host));
        wc_host_free(host);
        return 1;
    }
    printf("listening on %u\n", (unsigned)wc_host_local_port(host));
    fflush(stdout);
    wc_status status = wc_host_run(host);
    wc_host_free(host);
    return status == WC_OK ? 0 : 1;
}
