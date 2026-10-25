#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "tls/tls.h"

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

int main(void) {
    char directory[] = "/tmp/wrencote-tls-XXXXXX";
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

    wc_tls_context_free(NULL);
    wc_tls_conn_free(NULL);
    assert(wc_tls_alpn(NULL) == NULL);
    assert(wc_tls_handshake(NULL) == WC_TLS_ERROR);
    assert(wc_tls_read(NULL, NULL, 0, NULL) == WC_TLS_ERROR);
    assert(wc_tls_write(NULL, NULL, 0, NULL) == WC_TLS_ERROR);
    assert(wc_tls_context_new(NULL, key, NULL) == WC_EINVAL);
    assert(wc_tls_context_new(cert, NULL, NULL) == WC_EINVAL);
    assert(wc_tls_conn_new(NULL, 0, NULL) == WC_EINVAL);

    wc_tls_context *invalid = NULL;
    assert(wc_tls_context_new("missing", key, &invalid) == WC_EINVAL);
    wc_tls_context *server_context = NULL;
    assert(wc_tls_context_new(cert, key, &server_context) == WC_OK);
    wc_tls_context_enable_http2(server_context, true);

    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    set_nonblocking(sockets[0]);
    set_nonblocking(sockets[1]);

    wc_tls_conn *server_conn = NULL;
    assert(wc_tls_conn_new(server_context, sockets[1], &server_conn) == WC_OK);

    SSL_CTX *client_context = SSL_CTX_new(TLS_client_method());
    assert(client_context != NULL);
    assert(SSL_CTX_set_min_proto_version(client_context, TLS1_2_VERSION) == 1);
    const unsigned char protocols[] = {2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
    assert(SSL_CTX_set_alpn_protos(client_context, protocols, sizeof protocols) == 0);
    SSL *client = SSL_new(client_context);
    assert(client != NULL);
    assert(SSL_set_fd(client, sockets[0]) == 1);

    int server_done = 0;
    int client_done = 0;
    for (int i = 0; i < 1000 && !(server_done && client_done); i++) {
        if (!server_done) {
            wc_tls_result result = wc_tls_handshake(server_conn);
            if (result == WC_TLS_OK) {
                server_done = 1;
            } else {
                assert(result == WC_TLS_WANT_READ || result == WC_TLS_WANT_WRITE);
            }
        }
        if (!client_done) {
            int result = SSL_connect(client);
            if (result == 1) {
                client_done = 1;
            } else {
                int error = SSL_get_error(client, result);
                assert(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
            }
        }
        usleep(1000);
    }
    assert(server_done && client_done);
    assert(wc_tls_alpn(server_conn) != NULL);
    assert(strcmp(wc_tls_alpn(server_conn), "h2") == 0);

    const char ping[] = "ping";
    assert(SSL_write(client, ping, 4) == 4);
    char buffer[16];
    size_t got = 0;
    for (int i = 0; i < 1000 && got < 4; i++) {
        size_t amount = 0;
        wc_tls_result result = wc_tls_read(server_conn, buffer + got, sizeof buffer - got, &amount);
        got += amount;
        if (result != WC_TLS_OK && result != WC_TLS_WANT_READ) {
            assert(result == WC_TLS_WANT_WRITE);
        }
        usleep(1000);
    }
    assert(got == 4 && memcmp(buffer, "ping", 4) == 0);

    const char pong[] = "pong";
    size_t written = 0;
    while (written < 4) {
        size_t amount = 0;
        assert(wc_tls_write(server_conn, pong + written, 4 - written, &amount) == WC_TLS_OK);
        written += amount;
    }
    char reply[8];
    int reply_len = 0;
    for (int i = 0; i < 1000 && reply_len < 4; i++) {
        int amount = SSL_read(client, reply + reply_len, (int)(sizeof reply - (size_t)reply_len));
        if (amount > 0) {
            reply_len += amount;
        } else {
            int error = SSL_get_error(client, amount);
            assert(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
            usleep(1000);
        }
    }
    assert(reply_len == 4 && memcmp(reply, "pong", 4) == 0);

    SSL_free(client);
    SSL_CTX_free(client_context);
    wc_tls_conn_free(server_conn);
    close(sockets[0]);
    close(sockets[1]);
    wc_tls_context_free(server_context);
    unlink(cert);
    unlink(key);
    rmdir(directory);
    return 0;
}
