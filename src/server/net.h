#ifndef WC_SERVER_NET_H
#define WC_SERVER_NET_H

#include <wren.h>

#include "http/http.h"
#include "http/response.h"

typedef struct wc_req_object {
    const wc_http_request *request;
} wc_req_object;

typedef struct wc_res_object {
    wc_http_response *response;
} wc_res_object;

WrenForeignMethodFn wc_net_bind_method(void *user, const char *module, const char *class_name,
                                       bool is_static, const char *signature);
WrenForeignClassMethods wc_net_bind_class(void *user, const char *module, const char *class_name);

#endif
