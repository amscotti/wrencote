# `wrencote` CLI

```
wrencote run <script.wren> [--listen HOST:PORT] [--tls-cert FILE --tls-key FILE] [--allow http,fs,sys]
               [--root DIR] [--max-request BYTES] [--vm-heap BYTES]
               [--log-level 0-3] [-- <script args>]
wrencote check <script.wren> [--allow ...] [--root DIR]
wrencote --version | --help
```

- `run` binds the listener, loads the script, and runs until `Server.stop()` or a signal.
- `--tls-cert` and `--tls-key` serve HTTPS from a PEM certificate chain and private key. HTTP/2 is not
  advertised until the connection layer supports it; clients that offer both h2 and HTTP/1.1 negotiate
  HTTP/1.1. Supply both flags or neither.
- `check` compiles only; it does not execute `Server.serve` or any top-level code. Errors go to
  stderr with the file and line.
- `--allow` maps `http`/`net` to `WC_CAP_NET`, `fs` to `WC_CAP_FS`, `sys` to `WC_CAP_SYS`; `json`
  and `router` need no capability but are accepted for symmetry.
- `--root` is required when `fs` is allowed. Paths are relative to it.
- `--max-request` sets the request body and response cap in bytes.
- `--vm-heap` sets the Wren heap budget. The host collects garbage and answers 503 while the live
  heap stays above it; a budget below the VM baseline is rejected at startup.
- `--log-level` is 0 (errors), 1 (warnings), 2 (info), 3 (debug).
- `--sandbox` (default) applies Landlock + seccomp before the script's top level runs (the entry
  file is read first, then executed sandboxed); `--no-sandbox` is for development and tests.
- `--listen` accepts `HOST:PORT` and `[v6-host]:PORT` (for example `[::1]:8080`; IPv6 listeners are
  dual-stack).
- Exit codes: 0 success, 1 runtime/setup failure, 2 usage error.

Example:

```sh
wrencote run examples/app.wren --listen 127.0.0.1:8080 --allow http,json
curl -s http://127.0.0.1:8080/health
curl -s http://127.0.0.1:8080/json
```
