#include <stdio.h>
#include <time.h>

#include "vm/vm.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int main(void) {
    const int rounds = 1000;
    double start = now_ms();
    for (int i = 0; i < rounds; i++) {
        wc_vm_config config = {0};
        config.heap_bytes = 8u * 1024u * 1024u;
        wc_vm *vm = NULL;
        if (wc_vm_new(&config, &vm) != WC_OK) {
            return 1;
        }
        wc_vm_free(vm);
    }
    double elapsed = now_ms() - start;
    printf("%d VMs in %.1f ms; %.3f ms/VM\n", rounds, elapsed, elapsed / (double)rounds);
    return 0;
}
