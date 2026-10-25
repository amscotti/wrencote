#define _GNU_SOURCE

#include "modules/fs.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "core/compiler.h"
#include "core/str.h"
#include "vm/vm.h"

#define WC_FS_MAX_PATH 4096u
#define WC_FS_MAX_FILE (1024u * 1024u)

static void fs_abort(WrenVM *vm, const char *format, ...) WC_PRINTF(2, 3);

static void fs_abort(WrenVM *vm, const char *format, ...) {
    char message[256];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(message, sizeof message, format, args);
    va_end(args);
    wrenSetSlotString(vm, 0, message);
    wrenAbortFiber(vm, 0);
}

static bool path_arg(WrenVM *vm, int slot, wc_str *path) {
    int length = 0;
    const char *data = wrenGetSlotBytes(vm, slot, &length);
    if (data == NULL || length <= 0 || (size_t)length > WC_FS_MAX_PATH) {
        fs_abort(vm, "path must be 1..4096 bytes");
        return false;
    }
    if (data[0] == '/') {
        fs_abort(vm, "absolute paths are not allowed");
        return false;
    }
    if (memchr(data, '\0', (size_t)length) != NULL) {
        fs_abort(vm, "path contains NUL");
        return false;
    }
    *path = (wc_str){data, (size_t)length};
    return true;
}

static int walk_open(int root_fd, const wc_str path, int flags, mode_t mode) {
    char buffer[WC_FS_MAX_PATH + 1];
    memcpy(buffer, path.data, path.len);
    buffer[path.len] = '\0';
    int current = dup(root_fd);
    if (current < 0) {
        return -1;
    }
    char *cursor = buffer;
    while (*cursor != '\0') {
        char *slash = strchr(cursor, '/');
        if (slash != NULL) {
            *slash = '\0';
        }
        if (cursor[0] == '\0' || strcmp(cursor, ".") == 0 || strcmp(cursor, "..") == 0) {
            close(current);
            errno = EACCES;
            return -1;
        }
        bool last = slash == NULL;
        int next = openat(current, cursor,
                          last ? (flags | O_CLOEXEC | O_NOFOLLOW)
                               : (O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW),
                          mode);
        int saved = errno;
        close(current);
        if (next < 0) {
            errno = saved;
            return -1;
        }
        current = next;
        if (last) {
            return current;
        }
        cursor = slash + 1;
    }
    close(current);
    errno = EINVAL;
    return -1;
}

static int open_beneath(int root_fd, const wc_str path, int flags, mode_t mode) {
    if (path.len == 1 && path.data[0] == '.') {
        return openat(root_fd, ".", flags | O_CLOEXEC, mode);
    }
    char buffer[WC_FS_MAX_PATH + 1];
    memcpy(buffer, path.data, path.len);
    buffer[path.len] = '\0';
#ifdef SYS_openat2
    struct open_how how;
    memset(&how, 0, sizeof how);
    how.flags = (uint64_t)(flags | O_CLOEXEC | O_NOFOLLOW);
    how.mode = mode;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    int fd = (int)syscall(SYS_openat2, root_fd, buffer, &how, sizeof how);
    if (fd >= 0 || (errno != ENOSYS && errno != EINVAL)) {
        return fd;
    }
#endif
    return walk_open(root_fd, path, flags, mode);
}

static void fs_error(WrenVM *vm, const char *operation, int error) {
    if (error == ELOOP || error == EXDEV) {
        fs_abort(vm, "%s: path escapes the configured root", operation);
    } else if (error == ENOENT) {
        fs_abort(vm, "%s: no such file", operation);
    } else if (error == EACCES) {
        fs_abort(vm, "%s: permission denied", operation);
    } else if (error == EISDIR) {
        fs_abort(vm, "%s: is a directory", operation);
    } else {
        fs_abort(vm, "%s: %s", operation, strerror(error));
    }
}

static void fs_read(WrenVM *vm) {
    int root_fd = wc_vm_fs_root(wrenGetUserData(vm));
    if (root_fd < 0) {
        fs_abort(vm, "fs is not configured");
        return;
    }
    wc_str path;
    if (!path_arg(vm, 1, &path)) {
        return;
    }
    int fd = open_beneath(root_fd, path, O_RDONLY, 0);
    if (fd < 0) {
        fs_error(vm, "Fs.read", errno);
        return;
    }
    struct stat status;
    if (fstat(fd, &status) != 0) {
        int saved = errno;
        close(fd);
        fs_error(vm, "Fs.read", saved);
        return;
    }
    if (!S_ISREG(status.st_mode)) {
        close(fd);
        fs_abort(vm, "Fs.read: not a regular file");
        return;
    }
    if (status.st_size < 0 || (uintmax_t)status.st_size > WC_FS_MAX_FILE) {
        close(fd);
        fs_abort(vm, "Fs.read: file exceeds 1 MiB");
        return;
    }
    size_t size = (size_t)status.st_size;
    char *data = malloc(size == 0 ? 1 : size);
    if (data == NULL) {
        close(fd);
        fs_abort(vm, "Fs.read: out of memory");
        return;
    }
    size_t have = 0;
    while (have < size) {
        ssize_t got = read(fd, data + have, size - have);
        if (got <= 0) {
            int saved = errno;
            free(data);
            close(fd);
            fs_error(vm, "Fs.read", saved == 0 ? EIO : saved);
            return;
        }
        have += (size_t)got;
    }
    close(fd);
    wrenSetSlotBytes(vm, 0, data, size);
    free(data);
}

static void fs_write(WrenVM *vm) {
    int root_fd = wc_vm_fs_root(wrenGetUserData(vm));
    if (root_fd < 0) {
        fs_abort(vm, "fs is not configured");
        return;
    }
    wc_str path;
    if (!path_arg(vm, 1, &path)) {
        return;
    }
    int length = 0;
    const char *data = wrenGetSlotBytes(vm, 2, &length);
    if (data == NULL || (size_t)length > WC_FS_MAX_FILE) {
        fs_abort(vm, "Fs.write: content exceeds 1 MiB");
        return;
    }
    int fd = open_beneath(root_fd, path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fs_error(vm, "Fs.write", errno);
        return;
    }
    size_t have = 0;
    while (have < (size_t)length) {
        ssize_t put = write(fd, data + have, (size_t)length - have);
        if (put <= 0) {
            int saved = errno;
            close(fd);
            fs_error(vm, "Fs.write", saved == 0 ? EIO : saved);
            return;
        }
        have += (size_t)put;
    }
    close(fd);
    wrenSetSlotNull(vm, 0);
}

static void fs_exists(WrenVM *vm) {
    int root_fd = wc_vm_fs_root(wrenGetUserData(vm));
    if (root_fd < 0) {
        fs_abort(vm, "fs is not configured");
        return;
    }
    wc_str path;
    if (!path_arg(vm, 1, &path)) {
        return;
    }
    int fd = open_beneath(root_fd, path, O_PATH, 0);
    if (fd >= 0) {
        close(fd);
        wrenSetSlotBool(vm, 0, true);
        return;
    }
    if (errno == ENOENT || errno == ENOTDIR) {
        wrenSetSlotBool(vm, 0, false);
        return;
    }
    fs_error(vm, "Fs.exists", errno);
}

static void fs_list(WrenVM *vm) {
    int root_fd = wc_vm_fs_root(wrenGetUserData(vm));
    if (root_fd < 0) {
        fs_abort(vm, "fs is not configured");
        return;
    }
    wc_str path;
    if (!path_arg(vm, 1, &path)) {
        return;
    }
    int fd = open_beneath(root_fd, path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) {
        fs_error(vm, "Fs.list", errno);
        return;
    }
    DIR *directory = fdopendir(fd);
    if (directory == NULL) {
        int saved = errno;
        close(fd);
        fs_error(vm, "Fs.list", saved);
        return;
    }
    wrenSetSlotNewList(vm, 0);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        wrenSetSlotBytes(vm, 1, entry->d_name, strlen(entry->d_name));
        wrenInsertInList(vm, 0, count, 1);
        count++;
    }
    closedir(directory);
}

static void fs_is_dir(WrenVM *vm) {
    int root_fd = wc_vm_fs_root(wrenGetUserData(vm));
    if (root_fd < 0) {
        fs_abort(vm, "fs is not configured");
        return;
    }
    wc_str path;
    if (!path_arg(vm, 1, &path)) {
        return;
    }
    int fd = open_beneath(root_fd, path, O_RDONLY | O_DIRECTORY, 0);
    if (fd >= 0) {
        close(fd);
        wrenSetSlotBool(vm, 0, true);
        return;
    }
    if (errno == ENOTDIR || errno == ENOENT || errno == EACCES || errno == ELOOP) {
        wrenSetSlotBool(vm, 0, false);
        return;
    }
    fs_error(vm, "Fs.isDir", errno);
}

int wc_fs_open_walk(int root_fd, const char *path, int flags, mode_t mode) {
    wc_str view = {path, strlen(path)};
    return walk_open(root_fd, view, flags, mode);
}

WrenForeignMethodFn wc_fs_bind_method(void *user, const char *module, const char *class_name,
                                      bool is_static, const char *signature) {
    (void)user;
    if (strcmp(module, "fs") != 0 || strcmp(class_name, "Fs") != 0 || !is_static) {
        return NULL;
    }
    if (strcmp(signature, "read(_)") == 0) {
        return fs_read;
    }
    if (strcmp(signature, "write(_,_)") == 0) {
        return fs_write;
    }
    if (strcmp(signature, "exists(_)") == 0) {
        return fs_exists;
    }
    if (strcmp(signature, "list(_)") == 0) {
        return fs_list;
    }
    if (strcmp(signature, "isDir(_)") == 0) {
        return fs_is_dir;
    }
    return NULL;
}
