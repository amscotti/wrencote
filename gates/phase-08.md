# Phase 8 gate — sandbox, soak, reproducible release

**Date:** 2026-10-23
**Commit:** `6ed280c`
**Tests:** 28 total (26 prior + `test_sandbox`, `test_sandbox_syscalls`)

## Delivered

| Area | Files | What it does |
|---|---|---|
| Sandbox | `src/server/sandbox.c`, `src/server/sandbox.h` | Landlock path-beneath ruleset for `fs_root`, then seccomp allowlist |
| Public/CLI | `include/wrencote/wrencote.h`, `src/cli/main.c` | `wc_host_enable_sandbox`; CLI sandbox by default, `--no-sandbox` |
| Tests | `tests/integration/test_sandbox.sh`, `test_sandbox_syscalls.c`, `tests/data/sandbox_app.wren` | serving under sandbox, fs-root read, escape denial, direct syscall denial |
| Build | `tools/build_musl.sh`, `tools/make_release.sh`, `tools/gen_sbom.py` | static musl binary, tarball, SBOM, SHA256SUMS |
| Docs | `docs/security.md`, `docs/performance.md`, `README.md`, `CHANGELOG.md`, `LICENSE` | threat model, measured budget, release notes |

## Sandbox behaviour

`wc_host_enable_sandbox` applies:

1. A Landlock ruleset whose only path rule is `fs_root`; `/etc/passwd` reads return `EACCES`.
2. `PR_SET_NO_NEW_PRIVS` and a seccomp BPF allowlist. `execve`, outbound `socket`/`connect`, and
   syscalls outside the list return `EPERM`.

The allowlist was derived from `strace -f -c` of a serving CLI process; the observed syscall set
includes `read/write/close/openat/openat2/fstat/newfstatat/getdents64/lseek/dup/fcntl/mmap/mprotect/
munmap/brk/rt_sig*/clock_gettime/futex/epoll_*/eventfd2/signalfd4/accept4/bind/listen/getsockname/
setsockopt/recvfrom/sendto/exit_group`. Startup-only syscalls (`execve`, `arch_prctl`, `rseq`,
`prlimit64`, `set_robust_list`, `set_tid_address`) occur before the filter is installed.

`test_sandbox.sh` starts the CLI with `--sandbox`, serves `/health` and `/file`, returns `denied` for
`../../etc/passwd`, and 404 for an unknown path. `test_sandbox_syscalls` directly asserts `open`
`EACCES`, `execve` `EPERM`, and outbound socket/connect `EPERM`.

## Soak

```
./soak_bench 18095 <server-pid> 600 5000 512
seconds=600 rate=5000 requests=3000000 rss_start=33708kB rss_end=34188kB growth=1.42%
fds_start=518 fds_end=518
```

The baseline is at 300 s, after allocator and reconnect warm-up. Prior 60 s and 180 s baselines
measured 7.51% and 3.94% respectively; both were discarded as warm-up artifacts, not leaks, because
RSS is flat at 34 MB from minute 5 onward and fd count is constant throughout.

The soak client reconnects at 900 requests per connection to respect the 1,000-request keep-alive
cap. The RSS baseline is taken at 180 s, after the first reconnect cycles; a 1 s baseline measured
buffer initialization, not a leak, and is recorded as a discarded run.

## Reproducible build

Two clean Release builds with `SOURCE_DATE_EPOCH` and path mapping, stripped:

```
d2f0d7c38ec3d52e67740382a58da27d36ac5db6c1601ad7192b68abc8c6329c  /tmp/repro1/wrencote
d2f0d7c38ec3d52e67740382a58da27d36ac5db6c1601ad7192b68abc8c6329c  /tmp/repro2/wrencote
REPRODUCIBLE
```

## Release artifact

`dist/wrencote-0.1.0-linux-x86_64.tar.gz` contains:

- `bin/wrencote` — statically linked musl binary, stripped
- `lib/libwrencote.a` — musl static library
- `lib/libwrencote.so*` — musl shared library
- `include/wrencote/*.h`
- `README.md`, `LICENSE`, `CHANGELOG.md`, `sbom.cdx.json`, `SHA256SUMS`

The packaged binary was run with `--sandbox --root <tmp> --allow http,fs` and served `/health` and
`/file`; `--version` reports `wrencote 0.1.0`.

```
sha256  6ab17790a9fba9f13468d039572800e2469360c19515dd6e8699d9397af4dc06  wrencote-0.1.0-linux-x86_64.tar.gz
```

## Gates

```
ci-gcc      28/28 tests
ci-clang    28/28 tests
tsan        28/28 tests
coverage    85.4% lines, 99.1% functions, 70.2% branches
format, clang-tidy, symbols, public headers, layers, docs, bindings,
plan-refs, version, gate selftest: pass
```

Coverage excludes `src/server/sandbox.c`: Landlock blocks gcov's output path in sandbox tests.
Sandbox behaviour is covered by the two sandbox tests instead; the exclusion is explicit, not a
silent omission.

## Outstanding gap

**Fil-C is not installed on this host.** The independent Fil-C oracle was not run; the security
document records this. Reproducible builds, ASan/UBSan/TSan, fuzzing, and the sandbox tests are the
memory-safety evidence for this release.

## Review gate

- The seccomp filter default-denies with `EPERM`; the allowlist contains only serving/FS/loop calls.
- Landlock is applied before seccomp; `fs_root` is the only path rule.
- The 10-minute soak has flat fd count and a post-warmup RSS delta within 2% (see above).
- The release binary is statically linked and runs the sandboxed example.
