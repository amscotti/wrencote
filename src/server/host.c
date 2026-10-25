#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wrencote/wrencote.h"

#include "core/mem.h"
#ifdef WC_ENABLE_HTTP_CLIENT
#include "modules/http.h"
#endif
#include "http/server.h"
#include "loop/loop.h"
#include "server/host_internal.h"

#include "http/internal.h"
#include "server/modules.h"
#include "server/net.h"
#include "vm/vm.h"

#define WC_DEFAULT_CONNECTIONS 1024u
#define WC_DEFAULT_HEAD (32u * 1024u)
#define WC_DEFAULT_BODY (1024u * 1024u)
#define WC_DEFAULT_HEAP (8u * 1024u * 1024u)

void wc_host_config_init(wc_host_config *config) {
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof *config);
    config->struct_size = sizeof *config;
    config->max_connections = WC_DEFAULT_CONNECTIONS;
    config->max_head_bytes = WC_DEFAULT_HEAD;
    config->max_body_bytes = WC_DEFAULT_BODY;
    config->max_response_bytes = WC_DEFAULT_BODY;
    config->vm_heap_bytes = WC_DEFAULT_HEAP;
}

static void invoke_handler(wc_host *host, const wc_http_request *request,
                           wc_http_response *response) {
    if (host->handler == NULL || host->handler_call == NULL) {
        wc_http_response_send(response, 503, "no handler\n", 11);
        return;
    }
    /* The heap budget is advisory at the allocator level; act on it here so a
     * runaway handler stops consuming requests until the GC brings the heap
     * back under the limit. */
    if (wc_vm_over_budget(host->vm)) {
        wc_vm_collect(host->vm);
    }
    if (wc_vm_over_budget(host->vm)) {
        (void)wc_http_response_set_header(response, "retry-after", 11, "1", 1);
        wc_http_response_send(response, 503, "heap budget exceeded\n", 21);
        return;
    }
    WrenVM *vm = wc_vm_raw(host->vm);
    wrenEnsureSlots(vm, 5);
    wrenSetSlotHandle(vm, 0, host->handler);
    wrenSetSlotHandle(vm, 3, host->req_class);
    wc_req_object *req_object = wrenSetSlotNewForeign(vm, 1, 3, sizeof *req_object);
    req_object->request = request;
    wrenSetSlotHandle(vm, 3, host->res_class);
    wc_res_object *res_object = wrenSetSlotNewForeign(vm, 2, 3, sizeof *res_object);
    res_object->response = response;
    wc_status status = wc_vm_call(host->vm, host->handler_call);
    if (wc_http_response_streaming(response)) {
        wc_http_response_stream_abort(response);
    }
    if (!wc_http_response_sent(response)) {
        const char *message =
            status == WC_OK ? "handler returned without a response" : "handler error";
        wc_http_response_send(response, 500, message, strlen(message));
    }
}

static void host_http_handler(void *user, wc_http_conn *conn, const wc_http_request *request,
                              wc_http_response *response) {
    (void)conn;
    invoke_handler(user, request, response);
}

static void host_h2_handler(void *user, wc_h2_server *server, int32_t stream_id, const char *method,
                            const char *path, const wc_h2_header *headers, size_t header_count,
                            const uint8_t *body, size_t body_len) {
    wc_host *host = user;
    wc_http_request request = {0};
    request.method = wc_str_from_cstr(method);
    request.target = wc_str_from_cstr(path);
    const char *question = strchr(path, '?');
    if (question != NULL) {
        request.path = wc_str_from_parts(path, (size_t)(question - path));
        request.query = wc_str_from_cstr(question + 1);
    } else {
        request.path = request.target;
    }
    request.version = wc_str_from_cstr("2");
    request.body = (const char *)body;
    request.body_len = body_len;
    request.keep_alive = true;
    for (size_t i = 0; i < header_count && i < WC_HTTP_MAX_HEADERS; i++) {
        request.headers[i].name = wc_str_from_cstr(headers[i].name);
        request.headers[i].value = wc_str_from_cstr(headers[i].value);
    }
    request.header_count = header_count < WC_HTTP_MAX_HEADERS ? header_count : WC_HTTP_MAX_HEADERS;
    wc_http_response response = {0};
    response.h2 = server;
    response.h2_stream = stream_id;
    wc_buf_init(&response.headers);
    invoke_handler(host, &request, &response);
    wc_buf_free(&response.headers);
}

wc_status wc_host_new(const wc_host_config *config, wc_host **out) {
    if (config == NULL || out == NULL) {
        return WC_EINVAL;
    }
    if (config->struct_size < sizeof *config) {
        return WC_EINVAL;
    }
    if (config->tls != NULL) {
        return WC_EINVAL;
    }
    if ((config->capabilities & WC_CAP_FS) != 0 && config->fs_root == NULL) {
        return WC_EINVAL;
    }
#ifdef WC_ENABLE_HTTP_CLIENT
    wc_http_client_init();
#endif

    (void)signal(SIGPIPE, SIG_IGN);
    wc_host *host = wc_mem_calloc(1, sizeof *host);
    if (host == NULL) {
        return WC_ENOMEM;
    }
    host->config = *config;
    if (host->config.max_connections == 0) {
        host->config.max_connections = WC_DEFAULT_CONNECTIONS;
    }
    if (host->config.max_head_bytes == 0) {
        host->config.max_head_bytes = WC_DEFAULT_HEAD;
    }
    if (host->config.max_body_bytes == 0) {
        host->config.max_body_bytes = WC_DEFAULT_BODY;
    }
    if (host->config.max_response_bytes == 0) {
        host->config.max_response_bytes = WC_DEFAULT_BODY;
    }
    if (host->config.vm_heap_bytes == 0) {
        host->config.vm_heap_bytes = WC_DEFAULT_HEAP;
    }

    wc_clock clock = {host->config.clock, host->config.clock_user};
    host->loop = wc_loop_new(host->config.clock == NULL ? NULL : &clock);
    if (host->loop == NULL) {
        wc_mem_free(host);
        return WC_ENOMEM;
    }
    host->root_fd = -1;
    host->start_ns = wc_loop_now_ns(host->loop);
    if ((host->config.capabilities & WC_CAP_FS) != 0) {
        host->root_fd = open(host->config.fs_root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (host->root_fd < 0) {
            wc_loop_free(host->loop);
            wc_mem_free(host);
            return WC_ENOENT;
        }
    }

    wc_vm_config vm_config = {
        .heap_bytes = host->config.vm_heap_bytes,
        .capabilities = host->config.capabilities,
        .module_path = NULL,
        .log = host->config.log,
        .log_user = host->config.log_user,
        .bind_user = host,
        .bind_method = wc_server_bind_method,
        .bind_class = wc_server_bind_class,
    };
    wc_status status = wc_vm_new(&vm_config, &host->vm);
    if (status == WC_OK) {
        wc_vm_set_runtime(host->vm, host->root_fd, host->start_ns);
    }
    if (status != WC_OK) {
        wc_loop_free(host->loop);
        wc_mem_free(host);
        return status;
    }
    if (wc_vm_over_budget(host->vm)) {
        /* The VM baseline alone exceeds the configured budget; every request
         * would be rejected forever. Fail at startup instead. */
        wc_vm_free(host->vm);
        wc_loop_free(host->loop);
        wc_mem_free(host);
        return WC_EINVAL;
    }

    *out = host;
    return WC_OK;
}

static wc_status host_serve_internal(wc_host *host, const char *bind_addr, uint16_t port,
                                     wc_tls_context *tls) {
    (void)tls;
    if (host == NULL) {
        return WC_EINVAL;
    }
    if (host->server != NULL) {
        return WC_EBUSY;
    }
    wc_http_server_config server_config = {
        .max_connections = host->config.max_connections,
        .max_head_bytes = host->config.max_head_bytes,
        .max_body_bytes = host->config.max_body_bytes,
        .max_response_bytes = host->config.max_response_bytes,
        .keepalive_requests = 1000,
        .idle_timeout_ms = 5000,
        .request_timeout_ms = 10000,
#ifdef WC_ENABLE_TLS
        .transport_factory = tls == NULL ? NULL : wc_tls_transport_new,
        .transport_user = tls,
#endif
        .h2_handler = host_h2_handler,
        .h2_user = host,
    };
    host->server = wc_http_server_new(host->loop, &server_config, host_http_handler, host);
    if (host->server == NULL) {
        return WC_ENOMEM;
    }
    wc_status status = wc_http_server_listen(host->server, bind_addr, port);
    if (status != WC_OK) {
        wc_http_server_free(host->server);
        host->server = NULL;
    }
    return status;
}

wc_status wc_host_serve(wc_host *host, const char *bind_addr, uint16_t port) {
    return host_serve_internal(host, bind_addr, port, NULL);
}

wc_status wc_host_serve_tls(wc_host *host, const char *bind_addr, uint16_t port,
                            const char *cert_path, const char *key_path) {
    if (host == NULL || cert_path == NULL || key_path == NULL) {
        return WC_EINVAL;
    }
    if (host->server != NULL) {
        return WC_EBUSY;
    }
#ifdef WC_ENABLE_TLS
    wc_tls_context *tls = NULL;
    wc_status status = wc_tls_context_new(cert_path, key_path, &tls);
    if (status != WC_OK) {
        return status;
    }
#ifdef WC_ENABLE_HTTP2
    wc_tls_context_enable_http2(tls, true);
#endif
    status = host_serve_internal(host, bind_addr, port, tls);
    if (status != WC_OK) {
        wc_tls_context_free(tls);
        return status;
    }
    host->tls = tls;
    return WC_OK;
#else
    (void)bind_addr;
    (void)port;
    return WC_EINVAL;
#endif
}

uint16_t wc_host_local_port(const wc_host *host) {
    return host == NULL ? 0 : wc_http_server_port(host->server);
}

/* Reads an embedded script without the sandbox applied yet. Keeping this
 * separate would allow sandbox-before-execute; see GAPS G21 for the current
 * load-then-sandbox ordering. */
static wc_status read_script(const char *script_path, char **out) {
    FILE *file = fopen(script_path, "rb");
    if (file == NULL) {
        return WC_ENOENT;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length < 0) {
        fclose(file);
        return WC_EIO;
    }
    char *source = wc_mem_malloc((size_t)length + 1);
    if (source == NULL) {
        fclose(file);
        return WC_ENOMEM;
    }
    size_t got = fread(source, 1, (size_t)length, file);
    fclose(file);
    source[got] = '\0';
    *out = source;
    return WC_OK;
}

wc_status wc_host_check(wc_host *host, const char *script_path) {
    if (host == NULL || script_path == NULL) {
        return WC_EINVAL;
    }
    char *source = NULL;
    wc_status status = read_script(script_path, &source);
    if (status != WC_OK) {
        return status;
    }
    status = wc_vm_check(host->vm, script_path, source);
    wc_mem_free(source);
    return status;
}

const char *wc_host_last_error(const wc_host *host) {
    return host == NULL ? "" : wc_vm_last_error(host->vm);
}

wc_status wc_host_set_args(wc_host *host, int argc, const char *const *argv) {
    if (host == NULL || argc < 0 || (argc > 0 && argv == NULL)) {
        return WC_EINVAL;
    }
    wc_vm_set_args(host->vm, argc, argv);
    return WC_OK;
}

wc_status wc_host_load(wc_host *host, const char *script_path) {
    if (host == NULL || script_path == NULL) {
        return WC_EINVAL;
    }
    char *source = NULL;
    wc_status status = read_script(script_path, &source);
    if (status != WC_OK) {
        return status;
    }
    status = wc_vm_interpret(host->vm, "main", source);
    wc_mem_free(source);
    return status;
}

wc_status wc_host_load_source(wc_host *host, const char *module, const char *source) {
    if (host == NULL || source == NULL) {
        return WC_EINVAL;
    }
    return wc_vm_interpret(host->vm, module == NULL ? "main" : module, source);
}

wc_status wc_host_run(wc_host *host) {
    if (host == NULL) {
        return WC_EINVAL;
    }
    return wc_loop_run(host->loop);
}

void wc_host_stop(wc_host *host) {
    if (host != NULL) {
        host->running = false;
        wc_loop_stop(host->loop);
    }
}

void wc_host_free(wc_host *host) {
    if (host == NULL) {
        return;
    }
#ifdef WC_ENABLE_HTTP_CLIENT
    wc_http_client_cleanup();
#endif
    if (host->server != NULL) {
        wc_http_server_free(host->server);
        host->server = NULL;
    }
    if (host->tls != NULL) {
#ifdef WC_ENABLE_TLS
        wc_tls_context_free(host->tls);
#endif
        host->tls = NULL;
    }
    if (host->handler != NULL) {
        wc_vm_release_handle(host->vm, host->handler);
        host->handler = NULL;
    }
    if (host->handler_call != NULL) {
        wc_vm_release_handle(host->vm, host->handler_call);
        host->handler_call = NULL;
    }
    if (host->req_class != NULL) {
        wc_vm_release_handle(host->vm, host->req_class);
        host->req_class = NULL;
    }
    if (host->res_class != NULL) {
        wc_vm_release_handle(host->vm, host->res_class);
        host->res_class = NULL;
    }
    if (host->root_fd >= 0) {
        close(host->root_fd);
        host->root_fd = -1;
    }
    wc_vm_free(host->vm);
    wc_loop_free(host->loop);
    wc_mem_free(host);
}
