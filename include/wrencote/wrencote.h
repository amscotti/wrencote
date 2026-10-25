#ifndef WRENCOTE_WRENCOTE_H
#define WRENCOTE_WRENCOTE_H

#include <stddef.h>
#include <stdint.h>

#include "wrencote/clock.h"
#include "wrencote/export.h"
#include "wrencote/modules.h"
#include "wrencote/status.h"
#include "wrencote/version.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wc_host wc_host;

/* Receives one log line without a trailing newline. Level: 0 error, 1 warn,
 * 2 info, 3 debug. */
typedef void (*wc_log_fn)(void *user, int level, const char *line, size_t len);

typedef struct wc_host_config {
    size_t struct_size;        /* set by wc_host_config_init */
    size_t max_connections;    /* default 1024 */
    size_t max_head_bytes;     /* default 32 KiB */
    size_t max_body_bytes;     /* default 1 MiB */
    size_t max_response_bytes; /* default 1 MiB */
    size_t vm_heap_bytes;      /* default 8 MiB */
    const char *fs_root;       /* NULL disables WC_CAP_FS */
    uint32_t capabilities;     /* WC_CAP_* */
    wc_clock_fn clock;         /* NULL selects the monotonic clock */
    void *clock_user;
    wc_log_fn log; /* NULL discards library logs */
    void *log_user;
    const void *tls; /* reserved; must be NULL in v0.1 */
} wc_host_config;

/** Fills [config] with defaults and the correct struct_size. */
WC_API void wc_host_config_init(wc_host_config *config);

/** Creates a host. Returns WC_EINVAL for a bad config, WC_ENOMEM on failure. */
WC_API wc_status wc_host_new(const wc_host_config *config, wc_host **out);

/** Loads and runs a script's top level. Module imports are capability-checked. */
WC_API wc_status wc_host_load(wc_host *host, const char *script_path);

/** Binds a listener. Call before wc_host_load so the script can install its handler.
 * Pass port 0 for an ephemeral port; read it with wc_host_local_port. */
WC_API wc_status wc_host_serve(wc_host *host, const char *bind_addr, uint16_t port);

/** Serves HTTPS with the given PEM certificate chain and private key. */
WC_API wc_status wc_host_serve_tls(wc_host *host, const char *bind_addr, uint16_t port,
                                   const char *cert_path, const char *key_path);

/** Returns the bound port, or 0 when no listener exists. */
WC_API uint16_t wc_host_local_port(const wc_host *host);

/** Compiles a script without executing its top level. Reports through the log sink. */
WC_API wc_status wc_host_check(wc_host *host, const char *script_path);

/** Last compile/runtime error text, or an empty string. */
WC_API const char *wc_host_last_error(const wc_host *host);

/** Sets the values returned by Sys.args. The strings are borrowed and must outlive the host. */
WC_API wc_status wc_host_set_args(wc_host *host, int argc, const char *const *argv);

/** Applies Landlock filesystem scope and a seccomp syscall allowlist to this process.
 * Call after loading the script and before wc_host_run. */
WC_API wc_status wc_host_enable_sandbox(wc_host *host);

/** Runs the event loop until wc_host_stop. */
WC_API wc_status wc_host_run(wc_host *host);

/** Stops the loop; safe from a signal handler. */
WC_API void wc_host_stop(wc_host *host);

/** Frees the host. Only valid after wc_host_run returns. */
WC_API void wc_host_free(wc_host *host);

#ifdef __cplusplus
}
#endif

#endif
