#!/usr/bin/env node
// smoke.js — KRN end-to-end smoke test client
//
// Connects to the GMA WS server, subscribes to a comprehensive set of
// pipeline patterns, and verifies that every key receives at least one
// update within the configured duration.
//
// Usage:
//   node smoke.js [--url ws://localhost:8080] [--duration 30] [--verbose]
//                 [--target market|engine]

'use strict';

const WebSocket = require('ws');

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------
const args = process.argv.slice(2);
function argVal(flag, fallback) {
  const idx = args.indexOf(flag);
  return idx !== -1 && idx + 1 < args.length ? args[idx + 1] : fallback;
}
const URL      = argVal('--url', 'ws://localhost:8080');
const DURATION = parseInt(argVal('--duration', '30'), 10);
const VERBOSE  = args.includes('--verbose');
const TARGET   = argVal('--target', 'market');

// ---------------------------------------------------------------------------
// Key sets, one per server. `market` is krn_server (krn-common: the market
// connector, NEXO/VALT). `engine` is krn_engine_server (the reference plugin:
// REF carries `sample` + reference.sum/sumsq, SYN carries `counter` +
// synthetic.sin/cos). Every key in the chosen set is gated; nothing is skipped.
//
// A key either must produce a non-null value, or carries
// `expectReject: /regex/` and must instead be refused with a matching error
// attributed to that key (and must produce no update).
// ---------------------------------------------------------------------------
const KEY_SETS = {
  market: [
    // Tier 1: Direct field subscriptions (raw tick + TA)
    { key: 1,  streamKey: 'NEXO', field: 'lastPrice',       label: 'NEXO/lastPrice' },
    { key: 2,  streamKey: 'NEXO', field: 'volume',          label: 'NEXO/volume' },
    { key: 3,  streamKey: 'NEXO', field: 'sma_5',           label: 'NEXO/sma_5' },
    { key: 4,  streamKey: 'NEXO', field: 'sma_20',          label: 'NEXO/sma_20' },
    { key: 5,  streamKey: 'NEXO', field: 'ema_9',           label: 'NEXO/ema_9' },
    { key: 6,  streamKey: 'NEXO', field: 'rsi_14',          label: 'NEXO/rsi_14' },
    { key: 7,  streamKey: 'NEXO', field: 'macd_line',       label: 'NEXO/macd_line' },
    { key: 8,  streamKey: 'NEXO', field: 'bollinger_upper', label: 'NEXO/bollinger_upper' },
    { key: 9,  streamKey: 'VALT', field: 'lastPrice',       label: 'VALT/lastPrice' },
    { key: 10, streamKey: 'VALT', field: 'lastPrice',       label: 'VALT/lastPrice' },

    // Tier 2: Worker pipelines
    { key: 20, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Worker(mean)',
      pipeline: [{ type: 'Worker', fn: 'mean' }] },
    { key: 21, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Worker(max)',
      pipeline: [{ type: 'Worker', fn: 'max' }] },
    { key: 22, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Worker(spread)',
      pipeline: [{ type: 'Worker', fn: 'spread' }] },
    { key: 23, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Worker(last→scale)',
      pipeline: [{ type: 'Worker', fn: 'last' }, { type: 'Worker', fn: 'scale', factor: 100 }] },

    // Tier 3: AtomicAccessor
    { key: 30, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Atomic(rsi_14)',
      pipeline: [{ type: 'AtomicAccessor', field: 'rsi_14' }] },
    { key: 31, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Atomic(ob.spread)',
      pipeline: [{ type: 'AtomicAccessor', field: 'ob.spread' }] },
    { key: 32, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Atomic(ob.level.bid.1.price)',
      pipeline: [{ type: 'AtomicAccessor', field: 'ob.level.bid.1.price' }] },

    // Tier 4: Interval polling
    { key: 40, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Interval(sma_5)',
      node: { type: 'Interval', ms: 1000,
              child: { type: 'AtomicAccessor', streamKey: 'NEXO', field: 'sma_5' } } },
    { key: 41, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Interval(ob.spread)',
      node: { type: 'Interval', ms: 500,
              child: { type: 'AtomicAccessor', streamKey: 'NEXO', field: 'ob.spread' } } },

    // Tier 5: Multi-symbol
    { key: 50, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Worker(mean)',
      pipeline: [{ type: 'Worker', fn: 'mean' }] },
    { key: 51, streamKey: 'VALT', field: 'lastPrice', label: 'VALT/Atomic(macd_line)',
      pipeline: [{ type: 'AtomicAccessor', field: 'macd_line' }] },
    { key: 52, streamKey: 'VALT', field: 'lastPrice', label: 'VALT/Worker(max)',
      pipeline: [{ type: 'Worker', fn: 'max' }] },

    // Tier 6: Aggregate fan-in. `arity` must equal the number of `inputs`
    // (join-correctness D2); arity 5 over one input is refused at build.
    { key: 60, streamKey: 'NEXO', field: 'lastPrice', label: 'NEXO/Aggregate(3)',
      node: { type: 'Aggregate', arity: 3,
              inputs: [{ type: 'Listener', streamKey: 'NEXO', field: 'lastPrice' },
                       { type: 'Listener', streamKey: 'NEXO', field: 'volume' },
                       { type: 'Listener', streamKey: 'NEXO', field: 'sma_5' }] } },
  ],

  engine: [
    // Tier 1: Direct field subscriptions
    { key: 1,  streamKey: 'REF', field: 'sample',  label: 'REF/sample' },
    { key: 2,  streamKey: 'SYN', field: 'counter', label: 'SYN/counter' },

    // Tier 2: Worker pipelines
    { key: 20, streamKey: 'REF', field: 'sample', label: 'REF/Worker(mean)',
      pipeline: [{ type: 'Worker', fn: 'mean' }] },
    { key: 23, streamKey: 'REF', field: 'sample', label: 'REF/Worker(last→scale)',
      pipeline: [{ type: 'Worker', fn: 'last' }, { type: 'Worker', fn: 'scale', factor: 100 }] },

    // Tier 3: AtomicAccessor
    { key: 30, streamKey: 'REF', field: 'sample', label: 'REF/Atomic(reference.sum)',
      node: { type: 'AtomicAccessor', streamKey: 'REF', field: 'reference.sum' } },
    { key: 31, streamKey: 'REF', field: 'sample', label: 'REF/Atomic(reference.sumsq)',
      node: { type: 'AtomicAccessor', streamKey: 'REF', field: 'reference.sumsq' } },

    // Tier 4: Interval polling
    { key: 40, streamKey: 'REF', field: 'sample', label: 'REF/Interval(reference.sum)',
      node: { type: 'Interval', ms: 500,
              child: { type: 'AtomicAccessor', streamKey: 'REF', field: 'reference.sum' } } },

    // Tier 5: Multi-stream
    { key: 51, streamKey: 'SYN', field: 'counter', label: 'SYN/Atomic(synthetic.sin)',
      node: { type: 'AtomicAccessor', streamKey: 'SYN', field: 'synthetic.sin' } },
    { key: 52, streamKey: 'SYN', field: 'counter', label: 'SYN/Worker(max)',
      pipeline: [{ type: 'Worker', fn: 'max' }] },

    // Tier 6: Aggregate fan-in, arity == inputs (join-correctness D2). REF
    // emits only `sample`, so the second input is SYN, joined by:"none".
    { key: 60, streamKey: 'REF', field: 'sample', label: 'REF+SYN/Aggregate(2)',
      node: { type: 'Aggregate', arity: 2, by: 'none',
              inputs: [{ type: 'Listener', streamKey: 'REF', field: 'sample' },
                       { type: 'Listener', streamKey: 'SYN', field: 'counter' }] } },
  ],
};

if (!Object.prototype.hasOwnProperty.call(KEY_SETS, TARGET)) {
  console.error(`[smoke] unknown --target '${TARGET}' (expected: ${Object.keys(KEY_SETS).join(', ')})`);
  process.exit(2);
}
const REQUESTS = KEY_SETS[TARGET];

// ---------------------------------------------------------------------------
// State tracking
// ---------------------------------------------------------------------------
const stats = new Map();
for (const r of REQUESTS) {
  stats.set(r.key, {
    label:       r.label,
    expectReject: r.expectReject || null,
    acked:       false,
    count:       0,       // update frames
    values:      0,       // update frames with a non-null value
    firstAt:     null,    // seconds since subscribe, first non-null value
    lastValue:   null,
    errors:      [],      // error frames attributed to this key
  });
}

let subscribeTime = null;
let subscribeAcks = 0;
let strayErrors   = [];   // error frames naming no key we sent
let strayUpdates  = [];   // update frames for a key we never sent
let earlyClose    = false;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
function log(msg) {
  console.log(`[smoke] ${msg}`);
}

function run() {
  log(`Connecting to ${URL} (target: ${TARGET}, ${REQUESTS.length} keys) ...`);
  const ws = new WebSocket(URL);

  ws.on('error', (err) => {
    log(`WebSocket error: ${err.message}`);
    log('FAIL');
    process.exit(1);
  });

  ws.on('open', () => {
    log(`Connected to ${URL}`);

    // Build subscribe payload — strip fields that are not part of the protocol
    const requests = REQUESTS.map(({ label, expectReject, ...rest }) => rest);
    const msg = JSON.stringify({ type: 'subscribe', requests });
    ws.send(msg);

    subscribeTime = Date.now();
    log(`Sent ${REQUESTS.length} subscribe requests`);
  });

  ws.on('message', (data) => {
    let msg;
    try {
      msg = JSON.parse(data.toString());
    } catch {
      log(`Unparseable message: ${data}`);
      strayErrors.push(`unparseable frame: ${data}`);
      return;
    }

    if (msg.type === 'subscribed') {
      subscribeAcks++;
      const s = stats.get(msg.key);
      if (s) s.acked = true;
      if (VERBOSE) log(`  ack key=${msg.key}`);
      return;
    }

    if (msg.type === 'error') {
      const detail = `${msg.where}: ${msg.message}`;
      const s = msg.key !== undefined ? stats.get(msg.key) : undefined;
      if (s) s.errors.push(detail);
      else strayErrors.push(msg.key !== undefined ? `key=${msg.key} ${detail}` : detail);
      log(`  ERROR key=${msg.key !== undefined ? msg.key : '-'} ${detail}`);
      return;
    }

    if (msg.type === 'canceled') {
      if (VERBOSE) log(`  canceled key=${msg.key}`);
      return;
    }

    if (msg.type === 'update') {
      const s = stats.get(msg.key);
      if (!s) {
        strayUpdates.push(msg.key);
        log(`  update for unknown key=${msg.key}`);
        return;
      }
      s.count++;
      if (msg.value !== null && msg.value !== undefined) {
        s.values++;
        if (s.firstAt === null) {
          s.firstAt = ((Date.now() - subscribeTime) / 1000).toFixed(1);
        }
      }
      s.lastValue = msg.value;

      if (VERBOSE) {
        const valStr = typeof msg.value === 'object'
          ? JSON.stringify(msg.value)
          : String(msg.value);
        log(`  key=${String(msg.key).padEnd(3)} ${String(msg.streamKey).padEnd(6)} `
          + `${s.label.padEnd(32)} = ${valStr}`);
      }
    }
  });

  ws.on('close', () => {
    if (!report._closing) {
      earlyClose = true;
      log('Connection closed by server before the duration elapsed');
    }
    report();
  });

  // Schedule shutdown after DURATION seconds
  setTimeout(() => {
    log(`\nDuration elapsed (${DURATION}s). Sending cancel...`);
    report._closing = true;

    // Cancel all keys
    const keys = REQUESTS.map(r => r.key);
    ws.send(JSON.stringify({ type: 'cancel', keys }));

    // Give the server a moment to ack cancels, then close
    setTimeout(() => {
      ws.close();
      report();
    }, 1000);
  }, DURATION * 1000);
}

// ---------------------------------------------------------------------------
// Verdict per key
// ---------------------------------------------------------------------------
function verdict(s) {
  if (s.expectReject) {
    if (s.count > 0) return { ok: false, why: `expected rejection, got ${s.count} update(s)` };
    if (s.acked) return { ok: false, why: 'expected rejection, got a subscribe ack' };
    if (s.errors.length === 0) return { ok: false, why: 'expected rejection, got nothing' };
    if (!s.errors.some(e => s.expectReject.test(e)))
      return { ok: false, why: `rejected, but not as ${s.expectReject}` };
    return { ok: true, why: 'REJECTED (expected)' };
  }
  if (s.errors.length > 0) return { ok: false, why: `error: ${s.errors[0]}` };
  if (s.values === 0)
    return { ok: false, why: s.count > 0 ? `${s.count} update(s), all null` : 'no updates' };
  return { ok: true, why: 'ok' };
}

// ---------------------------------------------------------------------------
// Report
// ---------------------------------------------------------------------------
function report() {
  // Prevent double-report
  if (report._done) return;
  report._done = true;

  console.log('');
  log('=== RESULTS ===');
  console.log('');

  const keyWidth   = 5;
  const labelWidth = 34;

  // Header
  console.log(
    '  ' +
    'Key'.padEnd(keyWidth) + '  ' +
    'Subscription'.padEnd(labelWidth) + '  ' +
    'Updates'.padStart(8) + '  ' +
    'Values'.padStart(8) + '  ' +
    'First (s)'.padStart(10) + '  ' +
    'Verdict'
  );
  console.log('  ' + '-'.repeat(keyWidth + 2 + labelWidth + 2 + 8 + 2 + 8 + 2 + 10 + 2 + 7));

  let totalUpdates = 0;
  let keysWithData = 0;
  let keysRejected = 0;
  const failed     = [];

  for (const [key, s] of stats) {
    totalUpdates += s.count;
    const v = verdict(s);
    if (v.ok && s.expectReject) keysRejected++;
    else if (v.ok) keysWithData++;
    else failed.push(`${key} (${v.why})`);

    const firstStr = s.firstAt !== null ? `${s.firstAt}` : '-';
    console.log(
      '  ' +
      String(key).padEnd(keyWidth) + '  ' +
      s.label.padEnd(labelWidth) + '  ' +
      String(s.count).padStart(8) + '  ' +
      String(s.values).padStart(8) + '  ' +
      firstStr.padStart(10) + '  ' +
      (v.ok ? v.why : `FAIL ${v.why}`)
    );
  }

  const expectRejects = REQUESTS.filter(r => r.expectReject).length;
  console.log('');
  log('=== SUMMARY ===');
  log(`Target: ${TARGET} | Duration: ${DURATION}s | Total updates: ${totalUpdates}`);
  log(`Keys with values: ${keysWithData}/${stats.size - expectRejects}`
    + ` | Asserted rejections: ${keysRejected}/${expectRejects}`);
  log(`Subscribe acks: ${subscribeAcks}/${REQUESTS.length - expectRejects}`);

  const problems = [...failed];
  if (strayErrors.length > 0)
    problems.push(...strayErrors.map(e => `unattributed error: ${e}`));
  if (strayUpdates.length > 0)
    problems.push(`updates for keys never sent: ${[...new Set(strayUpdates)].join(', ')}`);
  if (earlyClose) problems.push('server closed the connection early');

  if (problems.length > 0) {
    log(`Failures: ${problems.length}`);
    for (const p of problems) log(`  - ${p}`);
    log('FAIL');
    process.exit(1);
  }
  log('PASS');
  process.exit(0);
}

run();
