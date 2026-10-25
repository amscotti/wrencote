#ifndef WC_SERVER_MODULES_H
#define WC_SERVER_MODULES_H

#include <wren.h>

WrenForeignMethodFn wc_server_bind_method(void *user, const char *module, const char *class_name,
                                          bool is_static, const char *signature);
WrenForeignClassMethods wc_server_bind_class(void *user, const char *module,
                                             const char *class_name);

#endif
