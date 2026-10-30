#!/usr/bin/env python3
"""ENC-1559: krn_engine_server serves reference.* values over its WebSocket.

Starts `krn_engine_server 0 <conf>` (argv[1]=0 is not a port, so the config
file's wsPort binds), performs a real WebSocket handshake, subscribes to two
requests clocked by the REF stream's `sample` field that pull `reference.sum`
and `reference.sumsq`, and checks every value that comes back against the
closed form of ReferenceConnector's inputs (sample_n = 100 + n):

    reference.sum   S_n = sum_{i<=n} (100 + i)
    reference.sumsq Q_n = sum_{i<=n} (100 + i)^2

Warm-up values (n <= 3) arrive as null. Then it sends SIGTERM and requires
exit 0 with the "stopped" log line.

Usage: engine_server_reference_test.py <path-to-krn_engine_server>
"""

import base64
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

BOOT_TIMEOUT_S = 20
READ_TIMEOUT_S = 20
WANT_PER_KEY = 5
BASE = 100.0
WARMUP = 3


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def closed_forms(limit=10000):
    sums, sumsqs = {}, {}
    s = q = 0.0
    for n in range(limit):
        x = BASE + n
        s += x
        q += x * x
        if n > WARMUP:
            sums[s] = n
            sumsqs[q] = n
    return sums, sumsqs


class WsClient:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=READ_TIMEOUT_S)
        key = base64.b64encode(os.urandom(16)).decode()
        req = ("GET / HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nUpgrade: websocket\r\n"
               "Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n"
               "Sec-WebSocket-Version: 13\r\n\r\n" % (port, key))
        self.sock.sendall(req.encode())
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("connection closed during handshake")
            self.buf += chunk
        head, self.buf = self.buf.split(b"\r\n\r\n", 1)
        self.status = head.split(b"\r\n", 1)[0].decode()
        if " 101 " not in self.status + " ":
            raise RuntimeError("handshake refused: %s" % self.status)

    def send_text(self, text):
        payload = text.encode()
        mask = os.urandom(4)
        n = len(payload)
        if n < 126:
            hdr = struct.pack("!BB", 0x81, 0x80 | n)
        elif n < 65536:
            hdr = struct.pack("!BBH", 0x81, 0x80 | 126, n)
        else:
            hdr = struct.pack("!BBQ", 0x81, 0x80 | 127, n)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(hdr + mask + masked)

    def _need(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise RuntimeError("connection closed")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def recv_text(self):
        while True:
            b0, b1 = self._need(2)
            op, n = b0 & 0x0F, b1 & 0x7F
            if n == 126:
                n = struct.unpack("!H", self._need(2))[0]
            elif n == 127:
                n = struct.unpack("!Q", self._need(8))[0]
            if b1 & 0x80:
                self._need(4)  # servers do not mask; tolerate it anyway
            data = self._need(n)
            if op == 0x1:
                return data.decode()
            if op == 0x8:
                raise RuntimeError("server sent close")

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def request(key, computed):
    return {"key": key, "streamKey": "REF", "field": "sample",
            "node": {"type": "AtomicAccessor", "streamKey": "REF", "field": computed}}


def main():
    if len(sys.argv) != 2:
        print("usage: engine_server_reference_test.py <krn_engine_server>", file=sys.stderr)
        return 2
    server = sys.argv[1]
    problems = []
    sums, sumsqs = closed_forms()
    port = free_port()

    with tempfile.TemporaryDirectory() as workdir:
        conf = os.path.join(workdir, "engine_server.conf")
        with open(conf, "w") as fh:
            fh.write("wsPort=%d\nthreadPoolSize=2\nmetricsEnabled=false\n" % port)
        env = dict(os.environ)
        for key in ("FORUM_URL", "FORUM_TENANT_ID", "FORUM_AGENT_TOKEN"):
            env.pop(key, None)
        log = open(os.path.join(workdir, "server.log"), "w+b")
        argv = [server, "0", conf]
        print("$ %s" % " ".join(argv))
        proc = subprocess.Popen(argv, stdout=log, stderr=subprocess.STDOUT,
                                cwd=workdir, env=env)
        rc = None
        try:
            deadline = time.time() + BOOT_TIMEOUT_S
            while time.time() < deadline and proc.poll() is None:
                log.flush()
                with open(log.name, "rb") as fh:
                    if b"listening" in fh.read():
                        break
                time.sleep(0.1)
            if proc.poll() is not None:
                problems.append("server exited during boot (rc=%s)" % proc.returncode)
            else:
                ws = WsClient(port)
                print("handshake: %s" % ws.status)
                sub = json.dumps({"type": "subscribe",
                                  "requests": [request(1, "reference.sum"),
                                               request(2, "reference.sumsq")]})
                print(">>> %s" % sub)
                ws.send_text(sub)
                got = {1: [], 2: []}
                nulls = {1: 0, 2: 0}
                deadline = time.time() + READ_TIMEOUT_S
                while time.time() < deadline and min(len(v) for v in got.values()) < WANT_PER_KEY:
                    msg = ws.recv_text()
                    print("<<< %s" % msg)
                    frame = json.loads(msg)
                    if frame.get("type") == "error":
                        problems.append("server error frame: %s" % msg)
                        break
                    if frame.get("type") != "update":
                        continue
                    k = frame.get("key")
                    if k not in got:
                        problems.append("update for unknown key: %s" % msg)
                        continue
                    if frame.get("streamKey") != "REF":
                        problems.append("update on the wrong stream: %s" % msg)
                    v = frame.get("value")
                    if v is None:
                        nulls[k] += 1
                        continue
                    got[k].append(v)
                ws.close()

                for k, table, name in ((1, sums, "reference.sum"), (2, sumsqs, "reference.sumsq")):
                    if len(got[k]) < WANT_PER_KEY:
                        problems.append("%s: %d values, wanted %d" % (name, len(got[k]), WANT_PER_KEY))
                    ns = []
                    for v in got[k]:
                        if v not in table:
                            problems.append("%s: %r is not S_n/Q_n for any n > %d" % (name, v, WARMUP))
                        else:
                            ns.append(table[v])
                    if ns != sorted(set(ns)):
                        problems.append("%s: tick indices not strictly increasing: %s" % (name, ns))
                    print("%s: %d values at ticks %s (+%d warm-up nulls)"
                          % (name, len(got[k]), ns, nulls[k]))

                print("$ kill -TERM %d" % proc.pid)
                proc.send_signal(signal.SIGTERM)
                try:
                    rc = proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    problems.append("server did not exit within 15s of SIGTERM")
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=10)
            log.flush()
            log.seek(0)
            out = log.read().decode("utf-8", "replace")
            log.close()

    print("--- server log (exit=%s) ---\n%s" % (rc, out))
    if rc is not None and rc != 0:
        problems.append("exit code after SIGTERM was %d, wanted 0" % rc)
    if rc == 0 and "stopped" not in out:
        problems.append("no 'stopped' log line after SIGTERM")
    for problem in problems:
        print("FAIL: %s" % problem, file=sys.stderr)
    print("PASS" if not problems else "FAILED")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
