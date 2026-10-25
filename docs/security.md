# Security model

## Trust boundaries

- **HTTP input is untrusted.** The parser is bounded and fuzzed; the server has connection, header,
  body, and response caps.
- **Scripts are trusted code in v0.1.** Wren is a C VM; a memory-safety bug in the VM or a native
  binding is a host compromise. The Wren tree is pinned and unmodified, but it is not a sandbox
  against malicious scripts.
- **Native modules are compiled in.** There is no dynamic plugin loading. A host grants `net`, `fs`,
  and `sys` through capability bits; `json` and `router` are pure Wren and always safe to import.
- **The CLI sandboxes the process.** `wc_host_enable_sandbox` / `--sandbox` applies a Landlock
  ruleset limiting filesystem access to the configured `fs_root` and a seccomp allowlist that denies
  `execve`, outbound `connect`, and unknown syscalls. The entry script is read before the sandbox is
  installed and its top level runs sandboxed (`wc_host_load_source`).

## Mitigations

| Risk | Mitigation |
|---|---|
| Path escape | `openat2(RESOLVE_BENEATH\|RESOLVE_NO_SYMLINKS\|RESOLVE_NO_MAGICLINKS)`, `O_NOFOLLOW`; fallback component walk |
| Header/body abuse | request line 8 KiB, head 32 KiB, 100 headers, body 1 MiB, response 1 MiB |
| Slowloris | 10 s request deadline, 5 s idle timeout, 60 s stalled-write deadline, 400/408/413/431 |
| Outbound network | seccomp denies `socket`/`connect`; only the host-owned listener accepts |
| Process execution | seccomp denies `execve`/`execveat` |
| Filesystem beyond root | Landlock path-beneath rule for `fs_root`; `/etc` reads return `EACCES` under sandbox |
| Heap growth | `vm_heap_bytes` sheds requests with 503 after a forced GC; not a hard allocator cap |
| FD leaks | per-connection cleanup; 10-minute soak checks flat fd count |

## What is not mitigated

- **CPU denial of service.** A script can loop forever. There is no instruction budget; the host
  relies on the trusted-script model and process supervision.
- **Memory exhaustion.** `vm_heap_bytes` is a request-shedding budget: the host collects garbage and
  answers 503 while the live heap stays above it. It is not a hard allocator cap (Wren corrupts if its
  allocator is told to fail), so a script that retains over-budget memory can still drive the process
  into the OS limit. Hard limits belong to the process (`RLIMIT_AS`) and the deployment supervisor.
- **VM memory-safety bugs.** ASan/UBSan/TSan and fuzzing cover the host and parser, not every Wren
  path. Fil-C is the intended independent oracle; it was not available on the build host for this
  release.
- **TOCTOU after resolution.** `openat2` resolves and opens atomically; later file operations on the
  returned fd are on that object. Landlock is the backstop. The documented fallback walk is
  component-wise and not atomic with later pathname operations.
- **Side channels and timing.** Not addressed in v0.1.

## Reporting

Report suspected vulnerabilities privately to the maintainer before public disclosure. Include a
minimal script or HTTP request, the build commit, and the kernel version. v0.1.0 is a development
release; do not expose it to untrusted scripts.

## Outbound HTTP

The optional `http` module is gated by `WC_CAP_NET`. Once granted, a script can
request any `http`/`https` URL reachable from the host, including link-local and private
addresses. Non-HTTP libcurl protocols such as `file://` are rejected. There is no destination allowlist yet; treat `--allow net` as an
SSRF-capable capability and do not grant it to untrusted scripts. TLS
verification and redirects follow libcurl defaults, and the call blocks the
single event loop for its timeout (10 s default, 5 s connect; `Http.setTimeout`
raises it up to 300 s).
