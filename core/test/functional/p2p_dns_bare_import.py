#!/usr/bin/env python3
# Copyright (c) 2026 The RIGOR developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Regression tests for DNS seed bare-name A/AAAA import.

The regtest seed is dummySeed.invalid. This script intercepts that name
inside an unprivileged network namespace. It does not query production DNS.
"""

import base64
import configparser
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request
from pathlib import Path

from test_framework.test_framework import TMPDIR_PREFIX, _default_test_binary_path

P2P = 28760
IPV4_A = "2.0.0.1"
IPV4_B = "3.0.0.1"
IPV6 = "1122:3344:5566:7788:9900:aabb:ccdd:eeff"
UNROUTABLE = ("127.0.0.1", "10.1.2.3", "192.0.2.10")


def framework_paths():
    """Find rigord and a disposable datadir the way the functional tests do.

    BUILDDIR and EXEEXT come from test/config.ini, which the build writes.
    BITCOIND overrides that binary, matching BitcoinTestFramework.setup().
    The datadir uses the framework prefix and is not a fixed machine path.
    """
    config_file = Path(__file__).resolve().parent.parent / "config.ini"
    if not config_file.is_file():
        raise SystemExit(f"missing functional test config {config_file}")
    config = configparser.ConfigParser()
    config.read(config_file)
    environment = config["environment"]
    binary = os.environ.get("BITCOIND") or _default_test_binary_path(
        environment["BUILDDIR"], environment.get("EXEEXT", ""), "bitcoind", "rigord"
    )
    return Path(binary), Path(tempfile.mkdtemp(prefix=TMPDIR_PREFIX))


def enter_namespace():
    if os.environ.get("RIGOR_DNS_NS") == "1":
        return
    os.execvpe(
        "unshare",
        ["unshare", "-U", "-r", "-n", "-m", "--", sys.executable, *sys.argv],
        {**os.environ, "RIGOR_DNS_NS": "1"},
    )


def run(cmd):
    subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def qname(packet):
    offset = 12
    labels = []
    while offset < len(packet):
        length = packet[offset]
        if length == 0 or length & 0xC0:
            break
        offset += 1
        labels.append(packet[offset:offset + length].decode("ascii", "replace"))
        offset += length
    return ".".join(labels)


def question_end(packet):
    end = 12
    while end < len(packet):
        length = packet[end]
        end += 1
        if length == 0:
            break
        if length & 0xC0:
            end += 1
            break
        end += length
    return end + 4


def encode_rdata(address):
    if ":" in address:
        return 28, socket.inet_pton(socket.AF_INET6, address)
    return 1, socket.inet_aton(address)


class DnsFixture:
    def __init__(self):
        self.queries = []
        self.service = {"a": [], "aaaa": [], "mode": "nx"}
        self.bare = {"a": [], "aaaa": [], "mode": "nx"}
        self._stop = False
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind(("127.0.0.1", 53))
        self._sock.settimeout(0.2)
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def set_case(self, service, bare):
        self.queries.clear()
        self.service = {"a": [], "aaaa": [], "mode": "nx", **service}
        self.bare = {"a": [], "aaaa": [], "mode": "nx", **bare}

    def _spec_for(self, name):
        lowered = name.lower().rstrip(".")
        if lowered.startswith("x") and "dummyseed.invalid" in lowered:
            return self.service
        if lowered == "dummyseed.invalid":
            return self.bare
        return {"a": [], "aaaa": [], "mode": "nx"}

    def _response(self, query):
        name = qname(query)
        spec = self._spec_for(name)
        self.queries.append(name)
        end = question_end(query)
        qtype = struct.unpack("!H", query[end - 4:end - 2])[0]
        tid = query[:2]
        question = query[12:end]
        mode = spec.get("mode", "nx")
        if mode == "nx":
            return tid + b"\x81\x83" + b"\x00\x01\x00\x00\x00\x00\x00\x00" + question
        wanted = []
        if mode == "ok":
            if qtype in (1, 255):
                wanted.extend(spec.get("a", []))
            if qtype in (28, 255):
                wanted.extend(spec.get("aaaa", []))
        if not wanted:
            return tid + b"\x81\x80" + b"\x00\x01\x00\x00\x00\x00\x00\x00" + question
        answers = b""
        for address in wanted:
            atype, rdata = encode_rdata(address)
            answers += b"\xc0\x0c" + struct.pack("!HHIH", atype, 1, 30, len(rdata)) + rdata
        header = tid + b"\x81\x80" + struct.pack("!HH", 1, len(wanted)) + b"\x00\x00\x00\x00"
        return header + question + answers

    def _serve(self):
        while not self._stop:
            try:
                data, addr = self._sock.recvfrom(2048)
            except socket.timeout:
                continue
            except OSError:
                break
            self._sock.sendto(self._response(data), addr)

    def stop(self):
        self._stop = True
        self._sock.close()


class Node:
    def __init__(self, name, ip, rpc, extra, rigord, base_dir):
        self.name = name
        self.ip = ip
        self.rpc_port = rpc
        self.extra = extra
        self.rigord = rigord
        self.datadir = base_dir / name
        self.proc = None
        self._stdout = None

    def start(self, wipe=True):
        if wipe and self.datadir.exists():
            shutil.rmtree(self.datadir)
        self.datadir.mkdir(parents=True, exist_ok=True)
        cmd = [
            str(self.rigord), "-regtest", f"-datadir={self.datadir}", "-server=1",
            f"-bind={self.ip}", f"-port={P2P}",
            "-rpcbind=127.0.0.1", f"-rpcport={self.rpc_port}", "-rpcallowip=127.0.0.1",
            "-listen=1", "-listenonion=0", "-discover=0", "-dnsseed=0", "-fixedseeds=0",
            "-dbcache=16", "-peertimeout=999999999",
            *self.extra,
        ]
        self._stdout = (self.datadir / "stdout.log").open("w")
        self.proc = subprocess.Popen(cmd, stdout=self._stdout, stderr=subprocess.STDOUT)
        self._wait_rpc()

    def _cookie(self):
        user, password = (self.datadir / "regtest" / ".cookie").read_text().strip().split(":", 1)
        return user, password

    def rpc(self, method, params=None):
        user, password = self._cookie()
        body = json.dumps({"jsonrpc": "1.0", "id": "dns", "method": method, "params": params or []}).encode()
        auth = base64.b64encode(f"{user}:{password}".encode()).decode()
        req = urllib.request.Request(
            f"http://127.0.0.1:{self.rpc_port}/", data=body,
            headers={"Authorization": f"Basic {auth}", "Content-Type": "application/json"},
        )
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with opener.open(req, timeout=10) as response:
            payload = json.loads(response.read().decode())
        if payload.get("error"):
            raise RuntimeError(payload["error"])
        return payload["result"]

    def _wait_rpc(self):
        deadline = time.time() + 30
        last = "no attempt"
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError(f"{self.name} exited {self.proc.returncode}: {last}")
            try:
                self.rpc("getblockcount")
                return
            except Exception as exc:
                last = f"{type(exc).__name__}: {exc}"
                time.sleep(0.2)
        raise RuntimeError(f"{self.name} RPC timeout: {last}")

    def log_text(self):
        path = self.datadir / "regtest" / "debug.log"
        return path.read_text(errors="replace") if path.exists() else ""

    def wait_log(self, needle, timeout=15):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if needle in self.log_text():
                return True
            time.sleep(0.2)
        return False

    def addresses(self):
        return self.rpc("getnodeaddresses", [0])

    def stop(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.rpc("stop")
            except Exception:
                self.proc.terminate()
            try:
                self.proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5)
        if self._stdout:
            self._stdout.close()
            self._stdout = None


def has_address(addresses, ip):
    wanted = ip.lower()
    return any(str(item.get("address", "")).lower() == wanted and int(item.get("port", 0)) == P2P for item in addresses)


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def run_import_case(dns, rigord, base_dir, name, service, bare, expect_via, expect_present, expect_absent, expect_fetch):
    dns.set_case(service, bare)
    node = Node(name, "14.0.0.1", 19410, ["-dnsseed=1"], rigord, base_dir)
    try:
        node.start()
        if expect_via:
            assert_true(node.wait_log(f"via {expect_via}"), f"{name}: missing via {expect_via}\n{node.log_text()[-1500:]}")
        else:
            assert_true(node.wait_log("falling back to addr-fetch"), f"{name}: missing addr-fetch\n{node.log_text()[-1500:]}")
        text = node.log_text()
        fetch = "falling back to addr-fetch" in text
        assert_true(fetch is expect_fetch, f"{name}: addr-fetch present={fetch}")
        found = node.addresses()
        for ip in expect_present:
            assert_true(has_address(found, ip), f"{name}: missing {ip} in {found}")
        for ip in expect_absent:
            assert_true(not has_address(found, ip), f"{name}: unexpectedly stored {ip} in {found}")
        service_queries = [q for q in dns.queries if q.lower().startswith("x")]
        bare_queries = [q for q in dns.queries if q.lower().rstrip(".").endswith("dummyseed.invalid") and not q.lower().startswith("x")]
        assert_true(service_queries, f"{name}: service-bit name was not queried: {dns.queries}")
        assert_true(bare_queries, f"{name}: bare name was not queried: {dns.queries}")
        return text
    finally:
        node.stop()


def run_bootstrap(dns, rigord, base_dir):
    dns.set_case({"mode": "nx"}, {"mode": "ok", "a": [IPV4_A]})
    listener = Node("listener", f"{IPV4_A}:{P2P}", 19421, [], rigord, base_dir)
    client = Node("client", f"14.0.0.1:{P2P}", 19422, ["-dnsseed=1"], rigord, base_dir)
    try:
        listener.start()
        client.start()
        assert_true(client.wait_log("via bare-name"), "bootstrap did not import the bare name")
        assert_true("falling back to addr-fetch" not in client.log_text(), "bootstrap used addr-fetch")

        def connected():
            peers = client.rpc("getpeerinfo")
            return any(str(peer.get("addr", "")).startswith(f"{IPV4_A}:{P2P}") and peer.get("connection_type") == "outbound-full-relay" for peer in peers)

        deadline = time.time() + 20
        while time.time() < deadline and not connected():
            time.sleep(0.3)
        assert_true(connected(), f"no full-relay peer: {client.rpc('getpeerinfo')}")
        assert_true(all(peer.get("connection_type") != "addr-fetch" for peer in client.rpc("getpeerinfo")), "addr-fetch peer present")

        client.stop()
        peers_dat = client.datadir / "regtest" / "peers.dat"
        assert_true(peers_dat.exists() and peers_dat.stat().st_size > 0, "peers.dat was not written")
        client.extra = ["-dnsseed=0"]
        dns.set_case({"mode": "nx"}, {"mode": "nx"})
        client.start(wipe=False)
        deadline = time.time() + 20
        while time.time() < deadline and not connected():
            time.sleep(0.3)
        assert_true(connected(), "restart did not recover the peer from peers.dat")

        listener.stop()
        deadline = time.time() + 20
        while time.time() < deadline and client.rpc("getconnectioncount") != 0:
            time.sleep(0.3)
        assert_true(client.rpc("getconnectioncount") == 0, "disconnect was not detected")
        started = time.time()
        listener.start()
        deadline = time.time() + 20
        while time.time() < deadline and not connected():
            time.sleep(0.3)
        assert_true(connected(), "automatic reconnect failed")
        return round(time.time() - started, 1)
    finally:
        client.stop()
        listener.stop()


def run_shutdown(dns, rigord, base_dir):
    dns.set_case({"mode": "nx"}, {"mode": "ok", "a": [IPV4_A]})
    node = Node("shutdown", "14.0.0.1", 19430, ["-dnsseed=1"], rigord, base_dir)
    node.start()
    assert_true(node.wait_log("via bare-name"), "shutdown case did not finish DNS import")
    started = time.time()
    node.stop()
    assert_true(node.proc.poll() is not None, "node still running after stop")
    return round(time.time() - started, 1)


def main():
    enter_namespace()
    rigord, base_dir = framework_paths()
    if not rigord.is_file():
        raise SystemExit(f"missing {rigord}")
    run(["ip", "link", "set", "lo", "up"])
    for address in (IPV4_A, IPV4_B, "14.0.0.1", "15.0.0.1"):
        run(["ip", "addr", "add", f"{address}/32", "dev", "lo"])
    run(["ip", "addr", "add", f"{IPV6}/128", "dev", "lo"])
    resolv = base_dir / "resolv.conf"
    resolv.write_text("nameserver 127.0.0.1\noptions timeout:1 attempts:1\n")
    run(["mount", "--bind", str(resolv), "/etc/resolv.conf"])
    dns = DnsFixture()
    results = {}
    try:
        run_import_case(
            dns, rigord, base_dir, "service-only",
            {"mode": "ok", "a": [IPV4_B]}, {"mode": "nx"},
            "service-bit", [IPV4_B], [IPV4_A], False,
        )
        results["service_only"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "bare-a",
            {"mode": "nx"}, {"mode": "ok", "a": [IPV4_A]},
            "bare-name", [IPV4_A], [], False,
        )
        results["bare_a"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "bare-aaaa",
            {"mode": "nx"}, {"mode": "ok", "aaaa": [IPV6]},
            "bare-name", [IPV6], [IPV4_A], False,
        )
        results["bare_aaaa"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "both",
            {"mode": "ok", "a": [IPV4_B]}, {"mode": "ok", "a": [IPV4_A]},
            "service-bit", [IPV4_B], [IPV4_A], False,
        )
        results["both_prefer_service"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "both-nx",
            {"mode": "nx"}, {"mode": "nx"},
            None, [], [IPV4_A, IPV4_B], True,
        )
        results["both_nx"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "both-empty",
            {"mode": "empty"}, {"mode": "empty"},
            None, [], [IPV4_A], True,
        )
        results["both_empty"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "service-fail-bare-ok",
            {"mode": "nx"}, {"mode": "ok", "a": [IPV4_A]},
            "bare-name", [IPV4_A], [], False,
        )
        results["one_path_bare"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "service-ok-bare-fail",
            {"mode": "ok", "a": [IPV4_B]}, {"mode": "nx"},
            "service-bit", [IPV4_B], [], False,
        )
        results["one_path_service"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "unusable",
            {"mode": "nx"}, {"mode": "ok", "a": list(UNROUTABLE)},
            None, [], list(UNROUTABLE), True,
        )
        results["unusable"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "mixed-unusable",
            {"mode": "nx"}, {"mode": "ok", "a": [*UNROUTABLE, IPV4_A]},
            "bare-name", [IPV4_A], list(UNROUTABLE), False,
        )
        results["mixed"] = "PASS"
        run_import_case(
            dns, rigord, base_dir, "unroutable-service-hides-nothing",
            {"mode": "ok", "a": ["10.9.8.7"]}, {"mode": "ok", "a": [IPV4_A]},
            "bare-name", [IPV4_A], ["10.9.8.7"], False,
        )
        results["unroutable_service"] = "PASS"
        duplicate_log = run_import_case(
            dns, rigord, base_dir, "duplicate",
            {"mode": "nx"}, {"mode": "ok", "a": [IPV4_A, IPV4_A]},
            "bare-name", [IPV4_A], [], False,
        )
        assert_true("Imported 1 addresses from DNS seed" in duplicate_log, duplicate_log[-800:])
        results["duplicate"] = "PASS"
        results["reconnect_seconds"] = run_bootstrap(dns, rigord, base_dir)
        results["bootstrap"] = "PASS"
        results["shutdown_seconds"] = run_shutdown(dns, rigord, base_dir)
        results["shutdown"] = "PASS"
    finally:
        dns.stop()
        if results.get("shutdown") == "PASS":
            shutil.rmtree(base_dir, ignore_errors=True)
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
