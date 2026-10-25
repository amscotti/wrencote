#ifndef WC_MODULES_HTTP_H
#define WC_MODULES_HTTP_H

#include <wren.h>

void wc_http_client_init(void);
void wc_http_client_cleanup(void);

WrenForeignMethodFn wc_http_bind_method(void *user, const char *module, const char *class_name,
                                        bool is_static, const char *signature);

#endif
