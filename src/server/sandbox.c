#define _GNU_SOURCE

#include "server/sandbox.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "server/host_internal.h"

#ifndef LANDLOCK_ACCESS_FS_REFER
#define LANDLOCK_ACCESS_FS_REFER 0
#endif

static wc_status apply_landlock(wc_host *host) {
    uint64_t access = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_WRITE_FILE |
                      LANDLOCK_ACCESS_FS_READ_DIR | LANDLOCK_ACCESS_FS_MAKE_REG |
                      LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE |
                      LANDLOCK_ACCESS_FS_REMOVE_DIR;
    struct landlock_ruleset_attr ruleset = {.handled_access_fs = access};
    int fd = (int)syscall(SYS_landlock_create_ruleset, &ruleset, sizeof ruleset, 0);
    if (fd < 0) {
        return WC_EINVAL;
    }
    if (host->root_fd >= 0) {
        int root = openat(host->root_fd, ".", O_PATH | O_CLOEXEC);
        if (root < 0) {
            close(fd);
            return WC_EINVAL;
        }
        struct landlock_path_beneath_attr beneath;
        beneath.allowed_access = access;
        beneath.parent_fd = root;
        if (syscall(SYS_landlock_add_rule, fd, LANDLOCK_RULE_PATH_BENEATH, &beneath, 0) != 0) {
            close(root);
            close(fd);
            return WC_EINVAL;
        }
        close(root);
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        close(fd);
        return WC_EINVAL;
    }
    if (syscall(SYS_landlock_restrict_self, fd, 0) != 0) {
        close(fd);
        return WC_EINVAL;
    }
    close(fd);
    return WC_OK;
}

#define ALLOW(nr)                                                                                  \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (nr), 0, 1), BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)

static wc_status apply_seccomp(void) {
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        ALLOW(__NR_read),
        ALLOW(__NR_write),
        ALLOW(__NR_writev),
        ALLOW(__NR_close),
        ALLOW(__NR_openat),
        ALLOW(__NR_openat2),
        ALLOW(__NR_newfstatat),
        ALLOW(__NR_fstat),
        ALLOW(__NR_lseek),
        ALLOW(__NR_readlink),
        ALLOW(__NR_getdents64),
        ALLOW(__NR_dup),
        ALLOW(__NR_fcntl),
        ALLOW(__NR_mmap),
        ALLOW(__NR_munmap),
        ALLOW(__NR_mprotect),
        ALLOW(__NR_madvise),
        ALLOW(__NR_brk),
        ALLOW(__NR_rt_sigaction),
        ALLOW(__NR_rt_sigprocmask),
        ALLOW(__NR_rt_sigreturn),
        ALLOW(__NR_clock_gettime),
        ALLOW(__NR_gettimeofday),
        ALLOW(__NR_futex),
        ALLOW(__NR_getpid),
        ALLOW(__NR_gettid),
        ALLOW(__NR_getrandom),
        ALLOW(__NR_epoll_create1),
        ALLOW(__NR_epoll_ctl),
        ALLOW(__NR_epoll_wait),
        ALLOW(__NR_epoll_pwait),
        ALLOW(__NR_eventfd2),
        ALLOW(__NR_signalfd4),
        ALLOW(__NR_accept4),
        ALLOW(__NR_bind),
        ALLOW(__NR_listen),
        ALLOW(__NR_getsockname),
        ALLOW(__NR_setsockopt),
        ALLOW(__NR_recvfrom),
        ALLOW(__NR_sendto),
        ALLOW(__NR_sendmsg),
        ALLOW(__NR_recvmsg),
        ALLOW(__NR_shutdown),
        ALLOW(__NR_exit),
        ALLOW(__NR_exit_group),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA)),
    };
    struct sock_fprog program = {.len = (unsigned short)(sizeof filter / sizeof filter[0]),
                                 .filter = filter};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        return WC_EINVAL;
    }
    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program, 0, 0) != 0) {
        return WC_EINVAL;
    }
    return WC_OK;
}

wc_status wc_host_enable_sandbox(wc_host *host) {
    if (host == NULL) {
        return WC_EINVAL;
    }
    wc_status status = apply_landlock(host);
    if (status != WC_OK) {
        return status;
    }
    return apply_seccomp();
}
