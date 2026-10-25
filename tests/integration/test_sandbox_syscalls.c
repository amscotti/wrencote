#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "wrencote/wrencote.h"

int main(void) {
    char root[] = "/tmp/wrencote-sandbox-XXXXXX";
    assert(mkdtemp(root) != NULL);
    wc_host_config config;
    wc_host_config_init(&config);
    config.capabilities = WC_CAP_FS;
    config.fs_root = root;
    wc_host *host = NULL;
    assert(wc_host_new(&config, &host) == WC_OK);
    assert(wc_host_enable_sandbox(host) == WC_OK);

    errno = 0;
    int fd = open("/etc/passwd", O_RDONLY);
    assert(fd == -1);
    assert(errno == EACCES);

    char executable[] = "/bin/true";
    char *const argv[] = {executable, NULL};
    assert(execve(executable, argv, NULL) == -1);
    assert(errno == EPERM);

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd >= 0) {
        struct sockaddr_in address;
        memset(&address, 0, sizeof address);
        address.sin_family = AF_INET;
        address.sin_port = htons(1);
        address.sin_addr.s_addr = htonl(0x7f000001);
        assert(connect(socket_fd, (struct sockaddr *)&address, sizeof address) == -1);
        assert(errno == EPERM);
        close(socket_fd);
    } else {
        assert(errno == EPERM);
    }

    wc_host_free(host);
    printf("sandbox: open, exec, outbound socket denied\n");
    return 0;
}
