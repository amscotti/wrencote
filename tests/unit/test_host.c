#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "wrencote/wrencote.h"

static void log_sink(void *user, int level, const char *line, size_t len) {
    (void)line;
    (void)len;
    int *count = user;
    if (level == 2) {
        (*count)++;
    }
}

int main(void) {
    wc_host_config config;
    wc_host_config_init(&config);
    assert(config.struct_size == sizeof config);
    assert(config.max_connections == 1024);
    assert(config.max_head_bytes == 32 * 1024);
    assert(config.max_body_bytes == 1024 * 1024);
    assert(config.vm_heap_bytes == 8 * 1024 * 1024);

    wc_host *host = NULL;
    assert(wc_host_new(NULL, &host) == WC_EINVAL);
    assert(wc_host_new(&config, NULL) == WC_EINVAL);

    wc_host_config bad = config;
    bad.struct_size = 0;
    assert(wc_host_new(&bad, &host) == WC_EINVAL);

    bad = config;
    bad.tls = &config;
    assert(wc_host_new(&bad, &host) == WC_EINVAL);

    bad = config;
    bad.capabilities = WC_CAP_FS;
    bad.fs_root = NULL;
    assert(wc_host_new(&bad, &host) == WC_EINVAL);

    config.vm_heap_bytes = 1024 * 1024;
    assert(wc_host_new(&config, &host) == WC_OK);
    assert(host != NULL);
    assert(wc_host_load(host, "tests/data/hello.wren") == WC_OK);
    wc_host_stop(host);
    assert(wc_host_run(host) == WC_OK);
    wc_host_free(host);
    wc_host_free(NULL);
    wc_host_stop(NULL);

    assert(wc_host_load(NULL, "x") == WC_EINVAL);
    assert(wc_host_run(NULL) == WC_EINVAL);
    assert(wc_host_serve(NULL, NULL, 0) == WC_EINVAL);
    assert(wc_host_serve_tls(NULL, NULL, 0, "cert", "key") == WC_EINVAL);

    assert(wc_host_new(&config, &host) == WC_OK);
    assert(wc_host_serve(host, "not-an-address", 0) == WC_EINVAL);
    assert(wc_host_serve(host, "127.0.0.1", 0) == WC_OK);
    assert(wc_host_serve(host, "127.0.0.1", 0) == WC_EBUSY);
    assert(wc_host_serve_tls(host, "127.0.0.1", 0, "cert", "key") == WC_EBUSY);
    assert(wc_host_serve_tls(host, "127.0.0.1", 0, NULL, NULL) == WC_EINVAL);
    wc_host_free(host);

    assert(wc_host_new(&config, &host) == WC_OK);
    assert(wc_host_serve_tls(host, "127.0.0.1", 0, "missing-cert", "missing-key") == WC_EINVAL);
    wc_host_free(host);

    wc_host_config deny = config;
    deny.capabilities = 0;
    assert(wc_host_new(&deny, &host) == WC_OK);
    assert(wc_host_load(host, "tests/data/deny.wren") == WC_EPERM);
    wc_host_free(host);

    wc_host_config missing = config;
    assert(wc_host_new(&missing, &host) == WC_OK);
    assert(wc_host_load(host, "tests/data/nope.wren") == WC_ENOENT);
    wc_host_free(host);

    int log_count = 0;
    wc_host_config logged = config;
    logged.log = log_sink;
    logged.log_user = &log_count;
    assert(wc_host_new(&logged, &host) == WC_OK);
    assert(wc_host_load(host, "tests/data/print.wren") == WC_OK);
    assert(log_count > 0);
    wc_host_free(host);

    wc_host_config small = config;
    small.vm_heap_bytes = 512 * 1024;
    assert(wc_host_new(&small, &host) == WC_OK);
    assert(wc_host_load(host, "tests/data/hello.wren") == WC_OK);
    wc_host_free(host);
    return 0;
}
