#ifndef WC_SERVER_HOST_INTERNAL_H
#define WC_SERVER_HOST_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include <wren.h>

#include "http/server.h"
#include "loop/loop.h"
#include "tls/tls.h"
#include "vm/vm.h"
#include "wrencote/wrencote.h"

struct wc_host {
    wc_host_config config;
    wc_loop *loop;
    wc_vm *vm;
    wc_http_server *server;
    wc_tls_context *tls;
    WrenHandle *handler;
    WrenHandle *handler_call;
    WrenHandle *req_class;
    WrenHandle *res_class;
    int root_fd;
    int64_t start_ns;
    bool running;
};

#endif
