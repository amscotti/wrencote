#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "vm/vm.h"

static wc_vm *make_vm(size_t heap, uint32_t capabilities) {
    wc_vm_config config = {0};
    config.heap_bytes = heap;
    config.capabilities = capabilities;
    wc_vm *vm = NULL;
    assert(wc_vm_new(&config, &vm) == WC_OK);
    assert(vm != NULL);
    return vm;
}

int main(void) {
    wc_vm *vm = make_vm(1024 * 1024, 0);
    assert(wc_vm_heap_used(vm) > 0);
    assert(wc_vm_heap_peak(vm) >= wc_vm_heap_used(vm));
    assert(wc_vm_raw(vm) != NULL);
    assert(wc_vm_handle_count(vm) == 1);

    WrenHandle *handle = wc_vm_make_call_handle(vm, "call()");
    assert(handle != NULL);
    assert(wc_vm_handle_count(vm) == 2);
    wc_vm_release_handle(vm, handle);
    assert(wc_vm_handle_count(vm) == 1);
    for (int i = 0; i < 10000; i++) {
        WrenHandle *cycle = wc_vm_make_call_handle(vm, "call()");
        assert(cycle != NULL);
        wc_vm_release_handle(vm, cycle);
    }
    assert(wc_vm_handle_count(vm) == 1);

    assert(wc_vm_interpret(vm, "main",
                           "var x = 1 + 2\n"
                           "if (x != 3) Fiber.abort(\"bad\")\n") == WC_OK);

    assert(wc_vm_interpret(vm, "script",
                           "import \"test\" for Echo\n"
                           "System.print(Echo.ping(41))\n"
                           "System.print(Echo.new(7).value)\n") == WC_OK);

    assert(wc_vm_interpret(vm, "script2", "import \"fs\" for Fs\n") == WC_EPERM);
    assert(wc_vm_last_error(vm)[0] != '\0');

    wc_vm *reentry = make_vm(1024 * 1024, 0);
    assert(wc_vm_interpret(reentry, "script4",
                           "import \"test\" for Echo\n"
                           "Echo.reenter()\n") == WC_EIO);
    assert(strstr(wc_vm_last_error(reentry), "re-entry blocked") != NULL);
    wc_vm_free(reentry);

    wc_vm *budget = make_vm(128 * 1024, 0);
    wc_status budget_status =
        wc_vm_interpret(budget, "script5",
                        "var items = []\n"
                        "for (i in 1..50000) items.add(\"0123456789abcdef\")\n");
    assert(budget_status == WC_OK);
    assert(wc_vm_over_budget(budget));
    assert(wc_vm_heap_peak(budget) > 128 * 1024);
    wc_vm_free(budget);

    /* The predicate is live, not a latch: freeing above-limit memory clears it
     * (the host 503 gate would otherwise wedge permanently). */
    wc_vm *probe = make_vm(1024 * 1024, 0);
    assert(!wc_vm_over_budget(probe));
    void *block = wc_vm_realloc(NULL, 2 * 1024 * 1024, probe);
    assert(block != NULL);
    assert(wc_vm_over_budget(probe));
    (void)wc_vm_realloc(block, 0, probe);
    assert(!wc_vm_over_budget(probe));
    wc_vm_free(probe);

    wc_vm_config disk_config = {0};
    disk_config.heap_bytes = 1024 * 1024;
    disk_config.module_path = "tests/data/mods";
    wc_vm *disk = NULL;
    assert(wc_vm_new(&disk_config, &disk) == WC_OK);
    assert(wc_vm_interpret(disk, "script6",
                           "import \"helper\" for Helper\n"
                           "if (Helper.value != 9) Fiber.abort(\"bad module\")\n") == WC_OK);
    assert(wc_vm_interpret(disk, "script7", "import \"nope\" for Nope\n") == WC_EPERM);
    wc_vm_free(disk);

    wc_vm *caps = make_vm(1024 * 1024, 0);
    assert(wc_vm_interpret(caps, "script8",
                           "import \"json\" for Json\n"
                           "if (Json.encode(1) != \"1\") Fiber.abort(\"json\")\n") == WC_OK);
    wc_vm_free(caps);

    wc_vm_free(vm);
    wc_vm_free(NULL);
    assert(wc_vm_raw(NULL) == NULL);
    assert(wc_vm_over_budget(NULL) == false);
    assert(wc_vm_heap_peak(NULL) == 0);
    assert(wc_vm_new(NULL, NULL) == WC_EINVAL);
    assert(wc_vm_interpret(NULL, "m", "") == WC_EINVAL);
    assert(wc_vm_call(NULL, NULL) == WC_EINVAL);
    assert(wc_vm_make_call_handle(NULL, "x") == NULL);
    assert(wc_vm_handle_count(NULL) == 0);
    assert(wc_vm_heap_used(NULL) == 0);
    assert(strcmp(wc_vm_last_error(NULL), "") == 0);
    return 0;
}
