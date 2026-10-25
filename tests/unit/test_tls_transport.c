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
#include <sys/uio.h>
#include <unistd.h>

#include "loop/transport.h"
#include "tls/tls.h"

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    char directory[] = "/tmp/wrencote-tls-transport-XXXXXX";
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

    assert(wc_transport_read(NULL, NULL, 0) == -1);
    assert(wc_transport_writev(NULL, NULL, 0) == -1);
    assert(wc_transport_flush(NULL) == WC_EINVAL);
    wc_transport_close(NULL);
    wc_transport_free(NULL);
    assert(wc_transport_plain_new(-1) == NULL);

    wc_tls_context *context = NULL;
    assert(wc_tls_context_new(cert, key, &context) == WC_OK);
    wc_tls_context_enable_http2(context, true);
    assert(wc_tls_transport_new(NULL, 0) == NULL);
    assert(wc_tls_transport_new(context, -1) == NULL);

    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    set_nonblocking(sockets[0]);
    set_nonblocking(sockets[1]);
    wc_transport *server = wc_tls_transport_new(context, sockets[1]);
    assert(server != NULL);
    assert(wc_transport_flush(server) == WC_OK);
    char early[] = "x";
    struct iovec early_iov = {.iov_base = early, .iov_len = 1};
    assert(wc_transport_writev(server, &early_iov, 1) == -1);
    assert(errno == EAGAIN);

    SSL_CTX *client_context = SSL_CTX_new(TLS_client_method());
    assert(client_context != NULL);
    const unsigned char protocols[] = {2, 'h', '2'};
    assert(SSL_CTX_set_alpn_protos(client_context, protocols, sizeof protocols) == 0);
    SSL *client = SSL_new(client_context);
    assert(client != NULL);
    assert(SSL_set_fd(client, sockets[0]) == 1);

    int client_done = 0;
    char buffer[64];
    for (int i = 0; i < 1000 && !client_done; i++) {
        ssize_t got = wc_transport_read(server, buffer, sizeof buffer);
        assert(got <= 0);
        if (got < 0) {
            assert(errno == EAGAIN || errno == EINTR);
        }
        int result = SSL_connect(client);
        if (result == 1) {
            client_done = 1;
        } else {
            int error = SSL_get_error(client, result);
            assert(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
        }
        usleep(1000);
    }
    assert(client_done);
    assert(wc_transport_flush(server) == WC_OK);

    const char ping[] = "ping";
    assert(SSL_write(client, ping, 4) == 4);
    size_t got = 0;
    for (int i = 0; i < 1000 && got < 4; i++) {
        ssize_t amount = wc_transport_read(server, buffer + got, sizeof buffer - got);
        if (amount > 0) {
            got += (size_t)amount;
        } else {
            assert(amount < 0 && (errno == EAGAIN || errno == EINTR));
        }
        usleep(1000);
    }
    assert(got == 4 && memcmp(buffer, "ping", 4) == 0);

    char pong[] = "pong";
    struct iovec iov = {.iov_base = pong, .iov_len = 4};
    assert(wc_transport_writev(server, &iov, 1) == 4);
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

    assert(wc_transport_read(server, NULL, 0) == -1);
    SSL_shutdown(client);
    ssize_t closed = 0;
    for (int i = 0; i < 100 && closed == 0; i++) {
        closed = wc_transport_read(server, buffer, sizeof buffer);
        if (closed < 0) {
            assert(errno == EAGAIN || errno == EINTR);
            usleep(1000);
        }
    }
    assert(closed == 0);

    int dead_sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, dead_sockets) == 0);
    set_nonblocking(dead_sockets[1]);
    wc_transport *dead = wc_tls_transport_new(context, dead_sockets[1]);
    assert(dead != NULL);
    close(dead_sockets[0]);
    ssize_t failed = 0;
    for (int i = 0; i < 100 && failed == 0; i++) {
        failed = wc_transport_read(dead, buffer, sizeof buffer);
        if (failed < 0) {
            assert(errno == EAGAIN || errno == EIO || errno == EINTR);
            usleep(1000);
        }
    }
    assert(failed < 0);
    wc_transport_free(dead);

    wc_transport_close(server);
    wc_transport_free(server);
    SSL_free(client);
    SSL_CTX_free(client_context);
    wc_tls_context_free(context);
    close(sockets[0]);
    unlink(cert);
    unlink(key);
    rmdir(directory);
    return 0;
}
