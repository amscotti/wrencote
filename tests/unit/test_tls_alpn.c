#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tls/tls.h"

static void nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

static void handshake(wc_tls_context *context, const unsigned char *protocols, size_t length,
                      int expect_ok, const char *expected) {
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    nonblocking(sockets[0]);
    nonblocking(sockets[1]);
    wc_tls_conn *server = NULL;
    assert(wc_tls_conn_new(context, sockets[1], &server) == WC_OK);
    SSL_CTX *client_context = SSL_CTX_new(TLS_client_method());
    assert(client_context != NULL);
    assert(SSL_CTX_set_alpn_protos(client_context, protocols, (unsigned)length) == 0);
    SSL *client = SSL_new(client_context);
    assert(client != NULL);
    assert(SSL_set_fd(client, sockets[0]) == 1);

    int client_done = 0;
    int server_done = 0;
    for (int i = 0; i < 1000 && !(client_done && server_done); i++) {
        wc_tls_result result = wc_tls_handshake(server);
        if (result == WC_TLS_OK) {
            server_done = 1;
        } else if (result != WC_TLS_ERROR) {
            assert(result == WC_TLS_WANT_READ || result == WC_TLS_WANT_WRITE);
        }
        int client_result = SSL_connect(client);
        if (client_result == 1) {
            client_done = 1;
        } else {
            int error = SSL_get_error(client, client_result);
            if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) {
                break;
            }
        }
        usleep(1000);
    }
    if (expect_ok) {
        assert(server_done && client_done);
        assert(wc_tls_alpn(server) != NULL);
        assert(strcmp(wc_tls_alpn(server), expected) == 0);
    } else {
        assert(!server_done);
    }
    SSL_free(client);
    SSL_CTX_free(client_context);
    wc_tls_conn_free(server);
    close(sockets[0]);
    close(sockets[1]);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    char directory[] = "/tmp/wrencote-tls-alpn-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    char cert[256];
    char key[256];
    snprintf(cert, sizeof cert, "%s/cert.pem", directory);
    snprintf(key, sizeof key, "%s/key.pem", directory);
    char command[768];
    snprintf(command, sizeof command,
             "openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost "
             "-keyout %s -out %s >/dev/null 2>&1",
             key, cert);
    assert(system(command) == 0);
    wc_tls_context *context = NULL;
    assert(wc_tls_context_new(cert, key, &context) == WC_OK);
    assert(wc_tls_context_new(cert, key, NULL) == WC_EINVAL);
    wc_tls_context_enable_http2(NULL, false);
    wc_tls_context_enable_http2(context, true);

    const unsigned char http1[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    handshake(context, http1, sizeof http1, 1, "http/1.1");

    const unsigned char unknown[] = {4, 's', 'p', 'd', 'y'};
    handshake(context, unknown, sizeof unknown, 0, NULL);

    wc_tls_context_free(NULL);
    wc_tls_context_free(context);
    unlink(cert);
    unlink(key);
    rmdir(directory);
    return 0;
}
