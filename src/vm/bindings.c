#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "vm/vm.h"

typedef struct wc_binding {
    const char *module;
    const char *class_name;
    const char *signature;
    bool is_static;
    int arity;
    WrenForeignMethodFn fn;
} wc_binding;

typedef struct echo_data {
    double seed;
} echo_data;

static const wc_binding BINDINGS[] = {
    {"test", "Echo", "ping(_)", true, 1, NULL},
    {"test", "Echo", "value", false, 0, NULL},
    {"test", "Echo", "reenter()", true, 0, NULL},
};

bool wc_slot_check(WrenVM *vm, int arity) {
    if (wrenGetSlotCount(vm) < arity + 1) {
        wrenSetSlotString(vm, 0, "native binding: wrong arity");
        wrenAbortFiber(vm, 0);
        return false;
    }
    return true;
}

static void echo_value(WrenVM *vm) {
    if (!wc_slot_check(vm, 0)) {
        return;
    }
    echo_data *data = wrenGetSlotForeign(vm, 0);
    wrenSetSlotDouble(vm, 0, data->seed);
}

static void echo_ping(WrenVM *vm) {
    if (!wc_slot_check(vm, 1)) {
        return;
    }
    double value = wrenGetSlotDouble(vm, 1);
    wrenSetSlotDouble(vm, 0, value + 1.0);
}

static void echo_reenter(WrenVM *vm) {
    if (!wc_slot_check(vm, 0)) {
        return;
    }
    wc_vm *host = wrenGetUserData(vm);
    wc_status status = wc_vm_call(host, host->noop_handle);
    if (status == WC_EBUSY) {
        wrenSetSlotString(vm, 0, "re-entry blocked");
    } else {
        wrenSetSlotString(vm, 0, "re-entry allowed");
    }
    wrenAbortFiber(vm, 0);
}

static WrenForeignMethodFn lookup(const char *module, const char *class_name, bool is_static,
                                  const char *signature) {
    for (size_t i = 0; i < sizeof BINDINGS / sizeof BINDINGS[0]; i++) {
        const wc_binding *binding = &BINDINGS[i];
        if (binding->is_static != is_static) {
            continue;
        }
        if (strcmp(binding->module, module) != 0 || strcmp(binding->class_name, class_name) != 0 ||
            strcmp(binding->signature, signature) != 0) {
            continue;
        }
        if (strcmp(signature, "ping(_)") == 0) {
            return echo_ping;
        }
        if (strcmp(signature, "value") == 0) {
            return echo_value;
        }
        if (strcmp(signature, "reenter()") == 0) {
            return echo_reenter;
        }
    }
    return NULL;
}

static WrenForeignMethodFn bind_method(WrenVM *wren, const char *module, const char *class_name,
                                       bool is_static, const char *signature) {
    WrenForeignMethodFn method = lookup(module, class_name, is_static, signature);
    if (method != NULL) {
        return method;
    }
    wc_vm *vm = wrenGetUserData(wren);
    if (vm->bind_method != NULL) {
        wc_vm_bind_method_fn fallback = vm->bind_method;
        return fallback(vm->bind_user, module, class_name, is_static, signature);
    }
    return NULL;
}

static void echo_allocate(WrenVM *vm) {
    echo_data *data = wrenSetSlotNewForeign(vm, 0, 0, sizeof *data);
    data->seed = wrenGetSlotDouble(vm, 1);
}

static WrenForeignClassMethods bind_class(WrenVM *wren, const char *module,
                                          const char *class_name) {
    (void)wren;
    WrenForeignClassMethods methods = {NULL, NULL};
    if (strcmp(module, "test") == 0 && strcmp(class_name, "Echo") == 0) {
        methods.allocate = echo_allocate;
        return methods;
    }
    wc_vm *vm = wrenGetUserData(wren);
    if (vm->bind_class != NULL) {
        wc_vm_bind_class_fn fallback = vm->bind_class;
        return fallback(vm->bind_user, module, class_name);
    }
    return methods;
}

void wc_vm_install_bindings(WrenConfiguration *config) {
    config->bindForeignMethodFn = bind_method;
    config->bindForeignClassFn = bind_class;
}
