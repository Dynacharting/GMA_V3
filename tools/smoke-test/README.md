# GMA Smoke Test

End-to-end smoke test that connects to the GMA WebSocket server, subscribes to
all pipeline patterns, and verifies data flows through the full stack.

## Prerequisites

- Node.js 18+
- dependencies installed with `npm ci` (run from this directory)
- GMA server running and receiving feed data

## Install

```bash
cd tools/smoke-test
npm ci
```

`node_modules/` is **not** tracked in git (ENC-878); `npm ci` installs the exact
versions pinned in `package-lock.json`.

## Usage

```bash
node smoke.js [--url ws://localhost:8080] [--duration 30] [--verbose] [--target market|engine]
```

| Flag         | Default                | Description                          |
|--------------|------------------------|--------------------------------------|
| `--url`      | `ws://localhost:8080`  | GMA WebSocket server URL             |
| `--duration` | `30`                   | Seconds to collect updates           |
| `--verbose`  | off                    | Log every individual update received |
| `--target`   | `market`               | Key set: `market` for krn-common's `krn_server` (NEXO/VALT), `engine` for `krn_engine_server` (REF/SYN) |

Each server gets its own key set, chosen explicitly, because `krn_engine_server`
serves no market streams. Every key in the chosen set is gated; none is skipped.
Pointing a key set at the wrong server fails (0 keys with values).

## What It Tests

With `--target market` the script subscribes to 23 keys across 6 tiers
(`--target engine` sends 10 keys over the same tiers):

| Tier | Pattern          | Keys   | What it validates                        |
|------|------------------|--------|------------------------------------------|
| 1    | Direct field     | 1-10   | Raw tick + TA fields flow to clients     |
| 2    | Worker pipeline  | 20-23  | FunctionMap computations (mean, max, etc)|
| 3    | AtomicAccessor   | 30-32  | Store reads (TA, order book)             |
| 4    | Interval         | 40-41  | Timer-driven polling                     |
| 5    | Multi-symbol     | 50-52  | Independent symbol routing               |
| 6    | Aggregate        | 60     | Fan-in join; `arity` equals `inputs`     |

## Launch Sequence

### Option A: Local feed simulator

```bash
# Terminal 1 — feed simulator
cd ~/Github/feed-simulator/go-feed && go run ./cmd/feedsim -port 8100

# Terminal 2 — krn-common's krn_server
cd ~/Github/krn-common
GMA_FEED_URL=ws://localhost:8100/feed ./build/krn_server 8080

# Terminal 3 — smoke test
cd ~/Github/GMA_V3/tools/smoke-test
node smoke.js --url ws://localhost:8080 --duration 30 --verbose
```

### Option B: Public feed

```bash
# krn_server is krn-common's: run it from that checkout, smoke.js from this one
GMA_FEED_URL=wss://feed-sim.v3m.xyz/feed ./build/krn_server 8080
node tools/smoke-test/smoke.js --url ws://localhost:8080 --duration 30
```

## Expected Output

```
[smoke] Connected to ws://localhost:8080
[smoke] Sent 23 subscribe requests
...
[smoke] === RESULTS ===

  Key    Subscription                         Updates    Values   First (s)  Verdict
  ----------------------------------------------------------------------------------
  1      NEXO/lastPrice                           116       116         0.0  ok
  6      NEXO/rsi_14                              116       116         0.0  ok
  40     NEXO/Interval(sma_5)                      19        19         1.0  ok
  60     NEXO/Aggregate(3)                        348       348         0.0  ok
  ...

[smoke] === SUMMARY ===
[smoke] Target: market | Duration: 20s | Total updates: 2726
[smoke] Keys with values: 23/23 | Asserted rejections: 0/0
[smoke] Subscribe acks: 23/23
[smoke] PASS
```

## Exit Codes

- **0** — PASS: every key received a non-null value, or was refused as its
  `expectReject` pattern says (and got no update)
- **1** — FAIL: a key got no value, a key got an error, an expected rejection
  did not happen, an error or update named no key that was sent, or the
  server closed early
- **2** — unknown `--target`

## Run logs

Run output is **not** committed. A checked-in log of one past run cannot be kept true
by anything — there is no CI to regenerate it (server-side CI was decided against
workspace-wide; the local pre-push gate is the mechanism), so it silently goes stale the
next time `smoke.js` changes and then misleads the next reader. That is exactly what
happened to the old `SMOKE_RUN_LOG.md`, deleted in ENC-1000: written 2026-03-20, never
updated, still advertising symbols and fields (`BLITZ`, `ema_12`) that `smoke.js` stopped
using in 2026-05.

Run the script and read its own output; the shape to expect is under **Expected Output**
above. The old log is recoverable from git history if you ever want it (`git log --
tools/smoke-test/SMOKE_RUN_LOG.md`).
