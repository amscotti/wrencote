#ifndef WC_VM_VM_H
#define WC_VM_VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wren.h>

#include "core/status.h"
#include "wrencote/wrencote.h"

#define WC_VM_ERROR_MAX 256

typedef WrenForeignMethodFn (*wc_vm_bind_method_fn)(void *user, const char *module,
                                                    const char *class_name, bool is_static,
                                                    const char *signature);
typedef WrenForeignClassMethods (*wc_vm_bind_class_fn)(void *user, const char *module,
                                                       const char *class_name);

typedef struct wc_vm {
    WrenVM *vm;
    size_t heap_limit;
    size_t heap_used;
    size_t heap_peak;
    bool oom;
    bool denied;
    bool over_budget;
    uint32_t capabilities;
    const char *module_path;
    WrenHandle *noop_handle;
    size_t handle_count;
    int depth;
    wc_log_fn log;
    void *log_user;
    void *bind_user;
    int fs_root_fd;
    int64_t start_ns;
    int argc;
    const char *const *argv;
    wc_vm_bind_method_fn bind_method;
    wc_vm_bind_class_fn bind_class;
    char last_error[WC_VM_ERROR_MAX];
} wc_vm;

typedef struct wc_vm_config {
    size_t heap_bytes;
    uint32_t capabilities;
    const char *module_path; /* dev-only; NULL disables disk modules */
    wc_log_fn log;
    void *log_user;
    void *bind_user;
    int fs_root_fd;
    int64_t start_ns;
    int argc;
    const char *const *argv;
    wc_vm_bind_method_fn bind_method;
    wc_vm_bind_class_fn bind_class;
} wc_vm_config;

wc_status wc_vm_new(const wc_vm_config *config, wc_vm **out);
void wc_vm_free(wc_vm *vm);
WrenVM *wc_vm_raw(wc_vm *vm);

wc_status wc_vm_interpret(wc_vm *vm, const char *module, const char *source);
wc_status wc_vm_call(wc_vm *vm, WrenHandle *method);

WrenHandle *wc_vm_make_call_handle(wc_vm *vm, const char *signature);
void wc_vm_release_handle(wc_vm *vm, WrenHandle *handle);
/* Registers a handle created through the raw Wren API with the handle count. */
void wc_vm_track_handle(wc_vm *vm);
size_t wc_vm_handle_count(const wc_vm *vm);

size_t wc_vm_heap_used(const wc_vm *vm);
size_t wc_vm_heap_peak(const wc_vm *vm);
bool wc_vm_over_budget(const wc_vm *vm);
/* Forces a collection; used before rejecting requests over the heap budget. */
void wc_vm_collect(wc_vm *vm);
const char *wc_vm_last_error(const wc_vm *vm);
void *wc_vm_bind_user(const wc_vm *vm);
void wc_vm_set_runtime(wc_vm *vm, int fs_root_fd, int64_t start_ns);
int wc_vm_fs_root(const wc_vm *vm);
int64_t wc_vm_start_ns(const wc_vm *vm);
void wc_vm_set_args(wc_vm *vm, int argc, const char *const *argv);
int wc_vm_argc(const wc_vm *vm);
const char *const *wc_vm_argv(const wc_vm *vm);
wc_status wc_vm_check(wc_vm *vm, const char *module, const char *source);

bool wc_slot_check(WrenVM *vm, int arity);

void wc_vm_install_modules(WrenConfiguration *config);
void wc_vm_install_bindings(WrenConfiguration *config);
void *wc_vm_realloc(void *memory, size_t new_size, void *user_data);

#endif
