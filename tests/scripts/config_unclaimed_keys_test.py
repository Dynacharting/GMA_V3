#!/usr/bin/env python3
"""ENC-1595: krn_engine_server warns once per config key nobody claims, and starts.

Run 1 passes a conf holding engine keys plus `allowNegativePrices=true` (a key
that moved to krn-common's MarketConfig) and a misspelled `threadPoolSzie`
given twice. It requires exactly one `config.unclaimed_key` line for each of
the two keys, naming the file, before the "listening" line, and no other such
line. Run 2 passes only engine keys and requires none. Both runs must reach
"listening" and exit 0 on SIGTERM.

Usage: config_unclaimed_keys_test.py <path-to-krn_engine_server>
"""

import os
import signal
import socket
import subprocess
import sys
import tempfile
import time

BOOT_TIMEOUT_S = 20
MARK = "config.unclaimed_key"


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def run(server, workdir, name, body):
    """Boots the server on a conf, stops it; returns (problems, log lines)."""
    problems = []
    conf = os.path.join(workdir, name)
    with open(conf, "w") as fh:
        fh.write(body)
    env = dict(os.environ)
    for key in ("FORUM_URL", "FORUM_TENANT_ID", "FORUM_AGENT_TOKEN"):
        env.pop(key, None)
    logpath = os.path.join(workdir, name + ".log")
    with open(logpath, "wb") as log:
        argv = [server, "0", conf]
        print("$ %s" % " ".join(argv))
        proc = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT,
                                cwd=workdir, env=env)
        listening = False
        try:
            deadline = time.time() + BOOT_TIMEOUT_S
            while time.time() < deadline and proc.poll() is None:
                with open(logpath, "rb") as fh:
                    if b" listening" in fh.read():
                        listening = True
                        break
                time.sleep(0.1)
            if not listening:
                problems.append("%s: never reached 'listening' (rc=%s)" % (name, proc.poll()))
            else:
                proc.send_signal(signal.SIGTERM)
                rc = proc.wait(timeout=15)
                if rc != 0:
                    problems.append("%s: exit %d after SIGTERM, wanted 0" % (name, rc))
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=10)
    with open(logpath, "rb") as fh:
        lines = fh.read().decode("utf-8", "replace").splitlines()
    print("--- %s log ---\n%s" % (name, "\n".join(lines)))
    return problems, conf, lines


def main():
    if len(sys.argv) != 2:
        print("usage: config_unclaimed_keys_test.py <krn_engine_server>", file=sys.stderr)
        return 2
    server = sys.argv[1]
    problems = []
    with tempfile.TemporaryDirectory() as workdir:
        port = free_port()
        p, conf, lines = run(server, workdir, "dirty.conf",
                             "wsPort=%d\nthreadPoolSize=2\n"
                             "allowNegativePrices=true\n"
                             "threadPoolSzie=8\nthreadPoolSzie=9\n" % port)
        problems += p
        warned = [l for l in lines if MARK in l]
        listen_at = next((i for i, l in enumerate(lines) if " listening" in l), len(lines))
        for key in ("allowNegativePrices", "threadPoolSzie"):
            want = "WARN  %s key=%s file=%s " % (MARK, key, conf)
            hits = [i for i, l in enumerate(lines) if want in l]
            if len(hits) != 1:
                problems.append("dirty.conf: %d warnings for %s, wanted 1" % (len(hits), key))
            elif hits[0] > listen_at:
                problems.append("dirty.conf: the %s warning came after 'listening'" % key)
        if len(warned) != 2:
            problems.append("dirty.conf: %d %s lines, wanted 2" % (len(warned), MARK))

        port = free_port()
        p, _, lines = run(server, workdir, "clean.conf",
                          "wsPort=%d\nthreadPoolSize=2\nmetricsEnabled=false\n"
                          "logLevel=info\n" % port)
        problems += p
        warned = [l for l in lines if MARK in l]
        if warned:
            problems.append("clean.conf: %d %s lines, wanted 0" % (len(warned), MARK))

    for problem in problems:
        print("FAIL: %s" % problem, file=sys.stderr)
    print("PASS" if not problems else "FAILED")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
