#include "vm/vm.h"

#include <stdio.h>
#include <string.h>

#include "core/mem.h"

typedef struct ObjClosure ObjClosure;
ObjClosure *wrenCompileSource(WrenVM *vm, const char *module, const char *source, bool isExpression,
                              bool printErrors);

static void write_fn(WrenVM *wren, const char *text) {
    if (strcmp(text, "\n") == 0) {
        return;
    }
    wc_vm *vm = wrenGetUserData(wren);
    if (vm->log != NULL) {
        vm->log(vm->log_user, 2, text, strlen(text));
    } else {
        fputs(text, stdout);
    }
}

static void error_fn(WrenVM *wren, WrenErrorType type, const char *module, int line,
                     const char *message) {
    wc_vm *vm = wrenGetUserData(wren);
    const char *safe_module = module == NULL ? "?" : module;
    const char *safe_message = message == NULL ? "unknown error" : message;
    if (type == WREN_ERROR_COMPILE) {
        snprintf(vm->last_error, sizeof vm->last_error, "%s:%d: %s", safe_module, line,
                 safe_message);
    } else if (type == WREN_ERROR_RUNTIME) {
        snprintf(vm->last_error, sizeof vm->last_error, "%s", safe_message);
    } else {
        if (vm->log != NULL) {
            char trace[WC_VM_ERROR_MAX];
            snprintf(trace, sizeof trace, "  at %s:%d in %s", safe_module, line, safe_message);
            vm->log(vm->log_user, 0, trace, strlen(trace));
        }
        return;
    }
    if (vm->log != NULL) {
        vm->log(vm->log_user, 0, vm->last_error, strlen(vm->last_error));
    }
}

static wc_status map_result(wc_vm *vm, WrenInterpretResult result) {
    if (result == WREN_RESULT_SUCCESS) {
        return WC_OK;
    }
    if (vm->oom) {
        return WC_ENOMEM;
    }
    if (vm->denied) {
        return WC_EPERM;
    }
    return result == WREN_RESULT_COMPILE_ERROR ? WC_EINVAL : WC_EIO;
}

wc_status wc_vm_new(const wc_vm_config *config, wc_vm **out) {
    if (config == NULL || out == NULL) {
        return WC_EINVAL;
    }
    wc_vm *vm = wc_mem_calloc(1, sizeof *vm);
    if (vm == NULL) {
        return WC_ENOMEM;
    }
    vm->heap_limit = config->heap_bytes == 0 ? 8u * 1024u * 1024u : config->heap_bytes;
    vm->capabilities = config->capabilities;
    vm->module_path = config->module_path;
    vm->log = config->log;
    vm->log_user = config->log_user;
    vm->bind_user = config->bind_user;
    vm->bind_method = config->bind_method;
    vm->bind_class = config->bind_class;

    WrenConfiguration wren_config;
    wrenInitConfiguration(&wren_config);
    wren_config.writeFn = write_fn;
    wren_config.errorFn = error_fn;
    wren_config.reallocateFn = wc_vm_realloc;
    wren_config.userData = vm;
    wc_vm_install_modules(&wren_config);
    wc_vm_install_bindings(&wren_config);

    vm->vm = wrenNewVM(&wren_config);
    if (vm->vm == NULL) {
        wc_mem_free(vm);
        return WC_ENOMEM;
    }
    vm->noop_handle = wrenMakeCallHandle(vm->vm, "call()");
    vm->handle_count = vm->noop_handle == NULL ? 0 : 1;
    *out = vm;
    return WC_OK;
}

void wc_vm_free(wc_vm *vm) {
    if (vm == NULL) {
        return;
    }
    if (vm->noop_handle != NULL) {
        wrenReleaseHandle(vm->vm, vm->noop_handle);
        vm->handle_count--;
    }
    wrenFreeVM(vm->vm);
    wc_mem_free(vm);
}

WrenVM *wc_vm_raw(wc_vm *vm) {
    return vm == NULL ? NULL : vm->vm;
}

wc_status wc_vm_interpret(wc_vm *vm, const char *module, const char *source) {
    if (vm == NULL || module == NULL || source == NULL) {
        return WC_EINVAL;
    }
    vm->denied = false;
    vm->depth++;
    WrenInterpretResult result = wrenInterpret(vm->vm, module, source);
    vm->depth--;
    return map_result(vm, result);
}

wc_status wc_vm_call(wc_vm *vm, WrenHandle *method) {
    if (vm == NULL || method == NULL) {
        return WC_EINVAL;
    }
    if (vm->depth != 0) {
        return WC_EBUSY;
    }
    vm->depth++;
    WrenInterpretResult result = wrenCall(vm->vm, method);
    vm->depth--;
    return map_result(vm, result);
}

WrenHandle *wc_vm_make_call_handle(wc_vm *vm, const char *signature) {
    if (vm == NULL || signature == NULL) {
        return NULL;
    }
    WrenHandle *handle = wrenMakeCallHandle(vm->vm, signature);
    if (handle != NULL) {
        vm->handle_count++;
    }
    return handle;
}

void wc_vm_release_handle(wc_vm *vm, WrenHandle *handle) {
    if (vm == NULL || handle == NULL) {
        return;
    }
    wrenReleaseHandle(vm->vm, handle);
    if (vm->handle_count > 0) {
        vm->handle_count--;
    }
}

void wc_vm_track_handle(wc_vm *vm) {
    if (vm != NULL) {
        vm->handle_count++;
    }
}

void wc_vm_collect(wc_vm *vm) {
    if (vm != NULL && vm->vm != NULL) {
        wrenCollectGarbage(vm->vm);
    }
}

size_t wc_vm_handle_count(const wc_vm *vm) {
    return vm == NULL ? 0 : vm->handle_count;
}
size_t wc_vm_heap_used(const wc_vm *vm) {
    return vm == NULL ? 0 : vm->heap_used;
}
size_t wc_vm_heap_peak(const wc_vm *vm) {
    return vm == NULL ? 0 : vm->heap_peak;
}
bool wc_vm_over_budget(const wc_vm *vm) {
    /* Derived from the live heap so frees and GC always clear it. */
    return vm != NULL && vm->heap_used > vm->heap_limit;
}

const char *wc_vm_last_error(const wc_vm *vm) {
    return vm == NULL ? "" : vm->last_error;
}
void *wc_vm_bind_user(const wc_vm *vm) {
    return vm == NULL ? NULL : vm->bind_user;
}
void wc_vm_set_runtime(wc_vm *vm, int fs_root_fd, int64_t start_ns) {
    if (vm != NULL) {
        vm->fs_root_fd = fs_root_fd;
        vm->start_ns = start_ns;
    }
}
int wc_vm_fs_root(const wc_vm *vm) {
    return vm == NULL ? -1 : vm->fs_root_fd;
}
int64_t wc_vm_start_ns(const wc_vm *vm) {
    return vm == NULL ? 0 : vm->start_ns;
}
void wc_vm_set_args(wc_vm *vm, int argc, const char *const *argv) {
    if (vm != NULL) {
        vm->argc = argc;
        vm->argv = argv;
    }
}
int wc_vm_argc(const wc_vm *vm) {
    return vm == NULL ? 0 : vm->argc;
}
const char *const *wc_vm_argv(const wc_vm *vm) {
    return vm == NULL ? NULL : vm->argv;
}
wc_status wc_vm_check(wc_vm *vm, const char *module, const char *source) {
    if (vm == NULL || module == NULL || source == NULL) {
        return WC_EINVAL;
    }
    vm->depth++;
    ObjClosure *closure = wrenCompileSource(vm->vm, module, source, false, true);
    vm->depth--;
    return closure == NULL ? map_result(vm, WREN_RESULT_COMPILE_ERROR) : WC_OK;
}
