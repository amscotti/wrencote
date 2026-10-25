#include "server/modules.h"

#include "modules/fs.h"
#ifdef WC_ENABLE_HTTP_CLIENT
#include "modules/http.h"
#endif
#include "modules/sys.h"
#include "server/net.h"

WrenForeignMethodFn wc_server_bind_method(void *user, const char *module, const char *class_name,
                                          bool is_static, const char *signature) {
    WrenForeignMethodFn method = wc_net_bind_method(user, module, class_name, is_static, signature);
    if (method != NULL) {
        return method;
    }
    method = wc_fs_bind_method(user, module, class_name, is_static, signature);
    if (method != NULL) {
        return method;
    }
#ifdef WC_ENABLE_HTTP_CLIENT
    method = wc_http_bind_method(user, module, class_name, is_static, signature);
    if (method != NULL) {
        return method;
    }
#endif
    return wc_sys_bind_method(user, module, class_name, is_static, signature);
}

WrenForeignClassMethods wc_server_bind_class(void *user, const char *module,
                                             const char *class_name) {
    return wc_net_bind_class(user, module, class_name);
}
