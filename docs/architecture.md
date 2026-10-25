# Architecture (working map)

The runtime is a single-threaded event loop that owns one Wren VM. Scripts register handlers; the host
accepts connections, parses requests, and enters the VM only when idle.

## Invariants

| # | Invariant |
|---|---|
| I1 | Public headers expose opaque types only (the sized host config is the documented exception) |
| I2 | Shared library exports only allowlisted `wc_*` symbols; the static archive leaks no `wren*` |
| I3 | No host heap allocation inside a request except the arena and the single response copy |
| I4 | Every size/offset computation is overflow-checked |
| I5 | Modules require their capability; unknown modules are denied |
| I6 | `wc_vm_call` never runs inside a foreign method; `wrenInterpret` only before the loop |
| I7 | Every public function documents ownership and failure modes |
| I8 | No plan/task references in code, tests, or comments |
| I9 | Every parser has a fuzz target; every fix adds a corpus entry |
| I10 | The server process is sandboxed at release |
| I11 | An HTTP/2 stream cannot block another stream on the same connection |
| I12 | Every native binding matches `BINDINGS.md` and the C table |
| I13 | Scripts are trusted in v0.1/v0.2; this is a documented limitation |
| I14 | Connection memory is bounded per connection |
| I15 | Every handle is released; no VM pointer survives a slot call |

## Layers

`core` ← `loop` ← {`vm`, `http`} ← `server` ← `cli`; `tls` and `http2` attach at v0.2. The include
graph is enforced by `tools/check_layers.py`.

## Phase 1 scope

Only `core/version.c` and the public version header exist. Everything else is scaffolded by the plan.
