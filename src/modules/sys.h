#ifndef WC_MODULES_SYS_H
#define WC_MODULES_SYS_H

#include <wren.h>

WrenForeignMethodFn wc_sys_bind_method(void *user, const char *module, const char *class_name,
                                       bool is_static, const char *signature);

#endif
