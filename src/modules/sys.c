#include "modules/sys.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/clock.h"
#include "vm/vm.h"

static void sys_env(WrenVM *vm) {
    int length = 0;
    const char *name = wrenGetSlotBytes(vm, 1, &length);
    if (name == NULL || length <= 0 || (size_t)length > 255) {
        wrenSetSlotString(vm, 0, "Sys.env: name must be 1..255 bytes");
        wrenAbortFiber(vm, 0);
        return;
    }
    char key[256];
    memcpy(key, name, (size_t)length);
    key[length] = '\0';
    const char *value = getenv(key);
    if (value == NULL) {
        wrenSetSlotNull(vm, 0);
    } else {
        wrenSetSlotBytes(vm, 0, value, strlen(value));
    }
}

static void sys_args(WrenVM *vm) {
    wc_vm *state = wrenGetUserData(vm);
    wrenSetSlotNewList(vm, 0);
    for (int i = 0; i < wc_vm_argc(state); i++) {
        const char *arg = wc_vm_argv(state)[i];
        wrenSetSlotBytes(vm, 1, arg, strlen(arg));
        wrenInsertInList(vm, 0, i, 1);
    }
}

static void sys_sleep(WrenVM *vm) {
    if (wrenGetSlotType(vm, 1) != WREN_TYPE_NUM) {
        wrenSetSlotString(vm, 0, "Sys.sleep: milliseconds must be a number");
        wrenAbortFiber(vm, 0);
        return;
    }
    double milliseconds = wrenGetSlotDouble(vm, 1);
    if (milliseconds != milliseconds || milliseconds < 0 || milliseconds > 10000) {
        wrenSetSlotString(vm, 0, "Sys.sleep: milliseconds must be 0..10000");
        wrenAbortFiber(vm, 0);
        return;
    }
    struct timespec delay = {
        .tv_sec = (time_t)(milliseconds / 1000.0),
        .tv_nsec =
            (long)((milliseconds - (double)(time_t)(milliseconds / 1000.0) * 1000.0) * 1000000.0),
    };
    /* Retry on EINTR with the remaining time; surface any other failure
     * instead of silently not sleeping. */
    struct timespec remaining = delay;
    while (nanosleep(&remaining, &remaining) != 0) {
        if (errno != EINTR) {
            wrenSetSlotString(vm, 0, "Sys.sleep: sleep failed");
            wrenAbortFiber(vm, 0);
            return;
        }
    }
}

static void sys_exit(WrenVM *vm) {
    wc_vm *state = wrenGetUserData(vm);
    wc_host *host = wc_vm_bind_user(state);
    if (host != NULL) {
        wc_host_stop(host);
    }
    wrenSetSlotNull(vm, 0);
}

static void sys_clock(WrenVM *vm) {
    wc_vm *state = wrenGetUserData(vm);
    int64_t now = wc_clock_now_ns(NULL);
    wrenSetSlotDouble(vm, 0, (double)(now - wc_vm_start_ns(state)) / 1e9);
}

WrenForeignMethodFn wc_sys_bind_method(void *user, const char *module, const char *class_name,
                                       bool is_static, const char *signature) {
    (void)user;
    if (strcmp(module, "sys") != 0 || strcmp(class_name, "Sys") != 0 || !is_static) {
        return NULL;
    }
    if (strcmp(signature, "env(_)") == 0) {
        return sys_env;
    }
    if (strcmp(signature, "args") == 0) {
        return sys_args;
    }
    if (strcmp(signature, "clock") == 0) {
        return sys_clock;
    }
    if (strcmp(signature, "sleep(_)") == 0) {
        return sys_sleep;
    }
    if (strcmp(signature, "exit()") == 0) {
        return sys_exit;
    }
    return NULL;
}
