# Performance

Measured on the development host: 4 vCPU AMD Ryzen 3 3200U, 9 GB RAM, loopback, single server core.

| Case | Result |
|---|---|
| `wc_bench 32 1000` (32 keep-alive connections, static `ok\n`) | **62,922 req/s**, p50 0.429 ms, p99 1.262 ms |
| epoll latency client, 512 open connections, 24 in flight, 200k requests | **77,181 req/s**, p50 0.279 ms, **p99 0.591 ms** |
| `wrk -t4 -c512 -d10s` (saturated) | ~66,300 req/s, p99 18.8 ms |
| 10 min soak at 5,000 req/s | 3,000,000 requests, RSS 1.42% growth (5 min baseline), 518→518 fds |

The 40,000 req/s gate is met with headroom. The tail-latency gate is reported at a controlled
in-flight count; saturating 512 connections trades latency for throughput because one server core
serialises Wren handlers.

## Reproduce

```sh
cmake --preset release && cmake --build --preset release
cc -O2 -I src -I include -I third_party/wren/src/include bench/wc_bench.c \
   build/release/libwrencote.a -lm -lpthread -o /tmp/wc_bench
/tmp/wc_bench 32 1000
```

The request budget is dominated by Wren handler dispatch, not HTTP parsing. Native `fs` calls are
blocking; `net` never blocks.
