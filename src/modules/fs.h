#ifndef WC_MODULES_FS_H
#define WC_MODULES_FS_H

#include <sys/types.h>
#include <wren.h>

/* Test seam for the component-walk fallback used when openat2 is unavailable. */
int wc_fs_open_walk(int root_fd, const char *path, int flags, mode_t mode);

WrenForeignMethodFn wc_fs_bind_method(void *user, const char *module, const char *class_name,
                                      bool is_static, const char *signature);

#endif
