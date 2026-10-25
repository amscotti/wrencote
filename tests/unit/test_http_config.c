#include <assert.h>
#include <string.h>

#include "http/server.h"
#include "loop/loop.h"

int main(void) {
    wc_loop *loop = wc_loop_new(NULL);
    assert(loop != NULL);

    wc_http_server_config config;
    memset(&config, 0, sizeof config);
    wc_http_server *server = wc_http_server_new(loop, &config, NULL, NULL);
    assert(server != NULL);
    assert(wc_http_server_port(server) == 0);
    assert(wc_http_server_connections(server) == 0);
    assert(wc_http_server_listen(NULL, "127.0.0.1", 0) == WC_EINVAL);
    assert(wc_http_server_listen(server, "127.0.0.1", 0) == WC_OK);
    assert(wc_http_server_port(server) != 0);
    assert(wc_http_server_listen(server, "127.0.0.1", 0) == WC_EBUSY);

    wc_http_server_config invalid;
    memset(&invalid, 0, sizeof invalid);
    wc_http_server *second = wc_http_server_new(loop, &invalid, NULL, NULL);
    assert(second != NULL);
    assert(wc_http_server_listen(second, "not-an-address", 0) == WC_EINVAL);
    wc_http_server_free(second);

    wc_http_server_free(server);
    wc_http_server_free(NULL);
    assert(wc_http_server_new(NULL, &config, NULL, NULL) == NULL);
    assert(wc_http_server_new(loop, NULL, NULL, NULL) == NULL);
    assert(wc_http_server_port(NULL) == 0);
    assert(wc_http_server_connections(NULL) == 0);
    wc_loop_free(loop);
    return 0;
}
