#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "loop/transport.h"

int main(void) {
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    wc_transport *transport = wc_transport_plain_new(fds[0]);
    assert(transport != NULL);

    char ab[3] = "ab";
    char cd[3] = "cd";
    struct iovec iov[2];
    iov[0].iov_base = ab;
    iov[0].iov_len = 2;
    iov[1].iov_base = cd;
    iov[1].iov_len = 2;
    assert(wc_transport_writev(transport, iov, 2) == 4);
    assert(wc_transport_flush(transport) == WC_OK);
    assert(wc_transport_alpn(transport) == NULL);
    assert(wc_transport_alpn(NULL) == NULL);

    char buffer[8] = {0};
    assert(read(fds[1], buffer, sizeof buffer) == 4);
    assert(memcmp(buffer, "abcd", 4) == 0);

    assert(write(fds[1], "xy", 2) == 2);
    assert(wc_transport_read(transport, buffer, sizeof buffer) == 2);
    assert(memcmp(buffer, "xy", 2) == 0);

    assert(wc_transport_read(NULL, buffer, sizeof buffer) == -1);
    assert(wc_transport_writev(NULL, iov, 2) == -1);
    assert(wc_transport_flush(NULL) == WC_EINVAL);
    wc_transport_close(NULL);
    wc_transport_free(NULL);

    wc_transport_close(transport);
    assert(transport->fd == -1);
    wc_transport_free(transport);
    close(fds[1]);

    assert(wc_transport_plain_new(-1) == NULL);
    return 0;
}
