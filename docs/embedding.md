# Embedding Wrencote

## Install

```sh
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix /tmp/wrencote
```

This installs `include/wrencote/*.h`, `lib/libwrencote.a`, `lib/libwrencote.so`,
`lib/cmake/wrencote`, `lib/pkgconfig/wrencote.pc`, and `bin/wrencote`.

## Link

```sh
cc app.c -I/tmp/wrencote/include -L/tmp/wrencote/lib -lwrencote -lm -o app
LD_LIBRARY_PATH=/tmp/wrencote/lib ./app
```

The public headers are only `wrencote/wrencote.h` and its included
`status.h`, `modules.h`, `clock.h`, `version.h`, and `export.h`. No private header is required.

## Lifecycle

```c
wc_host_config config;
wc_host_config_init(&config);
config.capabilities = WC_CAP_NET;       /* http/net */
config.fs_root = "./public";            /* required with WC_CAP_FS */
config.log = log_sink;                  /* optional; level 0..3 */
wc_host *host = NULL;
wc_status status = wc_host_new(&config, &host);
```

Then:

1. `wc_host_serve(host, "127.0.0.1", 8080)` binds the listener. Port 0 asks the OS for an
   ephemeral port; `wc_host_local_port` returns it. Serve before load so `Server.serve` can install
   its handler.
2. `wc_host_set_args` supplies `Sys.args` if the script wants command-line values.
3. `wc_host_load(host, "app.wren")` compiles and runs the script's top level.
4. `wc_host_run(host)` blocks on the event loop. `wc_host_stop(host)` is safe from a signal handler
   or another thread.
5. `wc_host_enable_sandbox(host)` optionally applies Landlock + seccomp after load and before run.
6. `wc_host_free(host)` after run returns.

`wc_host_check(host, path)` compiles without executing the top level; it is what `wrencote check`
uses.

## Failures

`wc_status` is stable (`WC_OK`, `WC_EPERM`, `WC_ENOENT`, `WC_ENOMEM`, `WC_EINVAL`, ...).
`wc_strerror(status)` returns a printable name. `wc_host_last_error(host)` holds the last
compile/runtime error. Module imports without the required capability fail with `WC_EPERM`
before the script runs.

The library is single-threaded. Only `wc_host_stop` may be called from another thread or a signal
handler; every other call must happen on the loop thread. `Fs.*` calls block the loop for the
filesystem operation; `net` never blocks.

## Example

The complete program is `examples/embed.c` (under 60 lines). It serves `examples/app.wren` on
port 18090:

```sh
/tmp/wrencote/bin/wrencote run examples/app.wren --listen 127.0.0.1:8080 --allow http,json
```
