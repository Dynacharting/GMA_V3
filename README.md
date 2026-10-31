# KRN

**Version:** 3.0
**Author:** Nicholas Randall
**License:** Proprietary (All rights reserved) – see [LICENSE](./LICENSE)

---

## Overview

**KRN** is a high-performance **C++20** WebSocket server and library for
real-time, atomic market-analysis computations over streaming data. Clients
connect over WebSocket, submit JSON-encoded **request trees**, and the node
pipeline runs nested statistical / technical-indicator math on the live feed,
pushing results back as values change.

The codebase is split into a **domain-agnostic core engine** plus **pluggable
connectors**. This repo is the engine, its composition `krn_host`, the
connector-free `krn_engine_server` and a reference plugin. The market connector
(order book, technical analysis, feed adapters) and the `krn_server` binary live
in [krn-common](https://github.com/Dynacharting/krn-common); the data layer lives in
[krn-data](https://github.com/Dynacharting/krn-data). Adding a new data source
means writing a new connector — no engine changes.

```
  data feed ──▶ ingress (a connector's IngressRegistry factory)
                     │  Event{ streamKey, payload, type }
                     ▼
              Dispatcher ──▶ IEventComputer(s) ──▶ AtomicStore
                     │
                     ▼
   WS clients ──▶ ClientSession ──▶ TreeBuilder ──▶ node pipeline ──▶ Responder ──▶ WS
```

> For the deeper picture (event lifecycle, connector contract, registries, how
> to add a connector), read **[CLAUDE.md](./CLAUDE.md)** and
> **[docs/ARCHITECTURE.md](./docs/ARCHITECTURE.md)** — those are the
> authoritative references; this README is the quick start.

Key properties:

- **Atomic computation model** — each operation (`mean`, `rsi_14`, `sma_5`, an
  `AtomicAccessor` on an `ob.*` key, …) is an independent node, enabling
  fine-grained pipelining and reuse.
- **Composable request trees** — clients compose nested pipelines of nodes
  (Listener → Worker/Aggregate/AtomicAccessor → Responder).
- **Engine / connector split** — `libkrn_engine` is source-agnostic; plugins
  add computers, ingress kinds and provider namespaces. CMake include-scoping
  and `tools/check_engine_isolation.py` enforce that the engine never depends
  on a plugin header or the data layer.
- **WebSocket interface** — clients connect, subscribe, and receive continuous
  value updates.

---

## Repository structure

```
├── CMakeLists.txt              # Root build (engine + host + server + tests)
├── LICENSE                     # Proprietary, all-rights-reserved licence
├── include/krn/                # Engine public headers (libkrn_engine)
│   ├── engine/                 # IConnector, EngineRegistries, registries
│   ├── nodes/                  # INode, Listener, Worker, Aggregate, Interval, ...
│   ├── server/                 # WebSocketServer, ClientSession, RequestKey
│   ├── ws/                     # WsBridge, WSResponder
│   ├── rt/                     # ThreadPool, SPSCQueue
│   ├── runtime/                # ShutdownCoordinator
│   ├── util/                   # Config, Logger, Metrics
│   ├── Dispatcher.hpp  Event.hpp  AtomicStore.hpp  FunctionMap.hpp  TreeBuilder.hpp
│   └── ...
├── src/                        # Engine implementations (mirrors include/)
│   ├── core/                   # Dispatcher, TreeBuilder, Expr, builtins
│   ├── nodes/  server/  ws/  rt/  util/
│   └── host/Host.cpp           # krn_host — the engine composition every server shares
├── apps/engine_server/         # krn_engine_server — krn_host + the reference plugin
├── plugins/
│   └── reference/              # ReferenceConnector/Computer, SyntheticConnector
├── tests/                      # GoogleTest suites (one krn_tests binary)
└── docs/                       # ARCHITECTURE.md, CONNECTOR_REFACTOR.md, ...
```

---

## Prerequisites

- **CMake ≥ 3.20**
- **C++20 compiler** (GCC 12+, Clang 14+ — clang++ is preferred by `tools/compile.sh`)
- **Boost.Asio / Beast** — networking + WebSocket
- **RapidJSON** — JSON parsing
- **GoogleTest** (only when `KRN_BUILD_TESTS=ON`)
- **Python 3** (utility scripts)

Third-party deps are vendored under `third_party/` (override with
`-DTHIRD_PARTY_ROOT=...`).

---

## Build

```bash
# Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Debug + tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DKRN_BUILD_TESTS=ON
cmake --build build -j$(nproc)

# Or use the helper (prefers clang++)
./tools/compile.sh
```

CMake targets: `libkrn_engine.a`, `libkrn_host.a`, `libkrn_plugin_reference.a`,
the **`krn_engine_server`** binary, and `krn_tests`.

---

## Run

```bash
# Compiled defaults: wsPort=8080 (no config file loaded)
./build/krn_engine_server

# argv[1]=wsPort, argv[2]=configFile (argv wins over the file)
./build/krn_engine_server 9002
./build/krn_engine_server 9002 my.conf
```

It streams `REF` (`reference.tick`: field `sample`, keys `reference.sum` /
`reference.sumsq`) and `SYN` (`synthetic.tick`: field `counter`, keys
`synthetic.sin` / `synthetic.cos`).

### Configuration

Runtime config is an **INI `key=value` file** parsed by `src/util/Config.cpp`.
Engine keys include `wsPort`, `threadPoolSize`, `logLevel`,
`metricsEnabled`/`metricsIntervalSec`, memory bounds (`maxSymbols`,
`maxFieldsPerSymbol`), `atomicKeyNamespaceByField` and the `ingress.N.*`
entries. See **[CLAUDE.md](./CLAUDE.md) → Configuration** for the full table.
The market keys and example config (`conf/krn.conf`) are krn-common's.

---

## WebSocket protocol

Clients send a `subscribe` message whose `requests` array each carries a
**`streamKey`** (string, required — there is **no `symbol` alias**, ENC-50), a
**`field`** to trigger on, a per-request id (`key` int **or** `id` int|string,
not both), and an optional `pipeline`/`node` sub-tree.

```jsonc
// int-keyed subscribe
{ "type": "subscribe", "requests": [
  { "key": 1, "streamKey": "NEXO", "field": "lastPrice",
    "pipeline": [ { "type": "Worker", "fn": "mean" } ] }
] }
// → { "type": "subscribed", "key": 1 }
// → { "type": "update", "key": 1, "streamKey": "NEXO", "value": 24.83 }
```

```jsonc
// string-id subscribe (mirrored back as "requestId")
{ "type": "subscribe", "requests": [
  { "id": "r-NEXO-open", "streamKey": "NEXO", "field": "lastPrice" }
] }
// → { "type": "update", "requestId": "r-NEXO-open", "streamKey": "NEXO", "value": 24.83 }
```

Errors come back as `{"type":"error","where":"<stage>","message":"..."}` where
`<stage>` is `parse` / `type` / `subscribe` / `validate` / `build`.

With the market connector (krn-common), two atomic-key namespaces exist — **bare** keys (`lastPrice`, `bid`, `sma_5`,
`rsi_14`, `macd_line`, `bollinger_upper`, …) are Listener-subscribable, while
**`ob.*`** keys (`ob.best.bid.price`, `ob.spread`, …) are **pipeline-only** (read
via an `AtomicAccessor`, never a direct Listener). The push-vs-pull rule, the
canonical `ob.*`-in-a-chart pattern, and the exact emitted key names are in
krn-common's **[docs/atomic-keys.md](https://github.com/Dynacharting/krn-common/blob/main/docs/atomic-keys.md)**.

Derived FunctionMap builtins (`mean`, `sum`, `stddev`, …) are stored under the
bare function name by default — a single per-symbol slot shared by every field,
so two fields driving the same builtin overwrite each other. Set
`atomicKeyNamespaceByField = true` to key them `<field>.<fn>` instead; see
krn-common's [docs/atomic-keys.md](https://github.com/Dynacharting/krn-common/blob/main/docs/atomic-keys.md#derived-atomic-key-shape--atomickeynamespacebyfield-enc-1008)
for the migration, since the key is client-visible.

### Feeds

The TCP feed server and the vendor feed adapters (e.g. NASDAQ ITCH) are the
market connector's — see krn-common's
**[docs/feed-adapters.md](https://github.com/Dynacharting/krn-common/blob/main/docs/feed-adapters.md)** and
**[docs/writing-adapters.md](https://github.com/Dynacharting/krn-common/blob/main/docs/writing-adapters.md)**.

---

## Testing

```bash
cmake -B build -DKRN_BUILD_TESTS=ON && cmake --build build -j$(nproc)
cd build && ctest --output-on-failure

# A single suite
./krn_tests --gtest_filter="DispatchTest.*"
```

All gtest cases build into one `krn_tests` executable; ctest also runs the
script, isolation-gate and consumer-smoke cases.

---

## License & Contact

This code is **proprietary**:

> Copyright © 2025 Nicholas Randall
> All rights reserved.
> Personal, non-commercial viewing only; all other rights reserved.

For licensing inquiries, contact **Nicholas Randall**.
