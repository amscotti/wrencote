#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "modules/fs.h"
#include "wrencote/wrencote.h"

static void log_sink(void *user, int level, const char *line, size_t length) {
    (void)user;
    (void)level;
    fprintf(stderr, "log: %.*s\n", (int)length, line);
}

static wc_status run_script(uint32_t capabilities, const char *root, const char *script) {
    wc_host_config config;
    wc_host_config_init(&config);
    config.capabilities = capabilities;
    config.fs_root = root;
    config.vm_heap_bytes = 4 * 1024 * 1024;
    config.log = log_sink;
    wc_host *host = NULL;
    wc_status status = wc_host_new(&config, &host);
    if (status != WC_OK) {
        return status;
    }
    status = wc_host_load(host, script);
    wc_host_free(host);
    return status;
}

int main(void) {
    assert(run_script(0, NULL, "tests/data/json_app.wren") == WC_OK);
    assert(run_script(0, NULL, "tests/data/router_app.wren") == WC_OK);

    wc_host_config args_config;
    wc_host_config_init(&args_config);
    args_config.capabilities = WC_CAP_SYS;
    wc_host *args_host = NULL;
    assert(wc_host_new(&args_config, &args_host) == WC_OK);
    const char *args[] = {"one", "two"};
    assert(wc_host_set_args(args_host, 2, args) == WC_OK);
    assert(wc_host_load(args_host, "tests/data/args_app.wren") == WC_OK);
    wc_host_free(args_host);

    char root[] = "/tmp/wrencote-stdlib-XXXXXX";
    assert(mkdtemp(root) != NULL);
    char hello[512];
    snprintf(hello, sizeof hello, "%s/hello.txt", root);
    int fd = open(hello, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0);
    assert(write(fd, "hello\n", 6) == 6);
    close(fd);
    int root_fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    assert(root_fd >= 0);
    int walked = wc_fs_open_walk(root_fd, "hello.txt", O_RDONLY, 0);
    assert(walked >= 0);
    close(walked);
    errno = 0;
    assert(wc_fs_open_walk(root_fd, "../etc/passwd", O_RDONLY, 0) == -1);
    assert(errno == EACCES);
    close(root_fd);

    char link[512];
    snprintf(link, sizeof link, "%s/link", root);
    assert(symlink("/etc/passwd", link) == 0);
    char dir[512];
    snprintf(dir, sizeof dir, "%s/dir", root);
    assert(mkdir(dir, 0755) == 0);
    char big[512];
    snprintf(big, sizeof big, "%s/big.txt", root);
    fd = open(big, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0);
    assert(ftruncate(fd, 1024 * 1024 + 1) == 0);
    close(fd);

    assert(run_script(WC_CAP_FS | WC_CAP_SYS, root, "tests/data/fs_app.wren") == WC_OK);
    assert(run_script(WC_CAP_FS, root, "tests/data/fs_errors.wren") == WC_OK);
    assert(run_script(WC_CAP_FS, "/does/not/exist", "tests/data/fs_app.wren") == WC_ENOENT);
    assert(run_script(WC_CAP_SYS, NULL, "tests/data/sys_limits.wren") == WC_OK);

    char out[512];
    snprintf(out, sizeof out, "%s/out.txt", root);
    assert(access(out, F_OK) == 0);
    unlink(out);
    unlink(link);
    unlink(hello);
    unlink(big);
    char exact[512];
    snprintf(exact, sizeof exact, "%s/exact.txt", root);
    unlink(exact);
    rmdir(dir);
    rmdir(root);
    return 0;
}
