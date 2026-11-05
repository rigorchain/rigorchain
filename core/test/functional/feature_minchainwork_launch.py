#!/usr/bin/env python3
# Copyright (c) 2026 The Rigor Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Launch semantics of a minimum chain work equal to genesis work (SPEC-008 §8.3.1).

Regtest genesis work is 2 and each block adds 2, mirroring mainnet where the
launch gate is GetBlockProof(genesis). Nodes 0 and 1 use the genesis-work gate;
node 2 uses a gate of five blocks' work, like the old 2^35 mainnet floor.

With mocktime just after genesis, the genesis-work nodes leave IBD at height 0,
serve headers, and relay block 1 immediately. The control stays in IBD, answers
getheaders with an empty message, and ignores the chain until it reaches its gate.

This is a regtest analogue of the work gate. Regtest is a test chain, so
getblocktemplate skips the mainnet production policy. A passing run does not
prove that policy. See feature_gbt_mainnet_startup.py for the production path.
"""
from test_framework.address import ADDRESS_BCRT1_UNSPENDABLE
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    p2p_port,
    rpc_port,
)

GENESIS_WORK = 2
CONTROL_GATE = 5 * GENESIS_WORK


class MinChainWorkLaunchTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 3
        self.extra_args = [
            ["-minimumchainwork=0x02"],
            ["-minimumchainwork=0x02"],
            [f"-minimumchainwork={CONTROL_GATE:#x}"],
        ]

    def setup_network(self):
        self.setup_nodes()
        self.connect_nodes(1, 0)
        self.connect_nodes(2, 0)

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args)
        seen_p2p = set()
        seen_rpc = set()
        for n in self.nodes:
            p2p = p2p_port(n.index)
            rpc = rpc_port(n.index)
            if p2p in seen_p2p or rpc in seen_rpc:
                raise AssertionError(
                    f"node {n.index} was given a port already assigned "
                    f"(p2p 127.0.0.1:{p2p}, rpc 127.0.0.1:{rpc})"
                )
            seen_p2p.add(p2p)
            seen_rpc.add(rpc)
            self.log.info(f"Starting node {n.index} p2p=127.0.0.1:{p2p} rpc=127.0.0.1:{rpc}")
            try:
                n.start()
                n.wait_for_rpc_connection()
            except Exception as exc:
                detail = self._startup_failure_detail(n)
                raise AssertionError(
                    f"node {n.index} failed to start "
                    f"(p2p 127.0.0.1:{p2p}, rpc 127.0.0.1:{rpc}): {exc}\n{detail}"
                ) from exc
            if f"127.0.0.1:{rpc}" not in n.url:
                raise AssertionError(
                    f"node {n.index} RPC client url {n.url} does not use rpc port {rpc}"
                )
            log = n.debug_log_path.read_text(encoding="utf-8", errors="replace")
            if f"Bound to 127.0.0.1:{p2p}" not in log:
                raise AssertionError(
                    f"node {n.index} did not bind its own p2p port 127.0.0.1:{p2p}"
                )
            if "127.0.0.1:28762" in log or "127.0.0.1:18445" in log:
                raise AssertionError(
                    f"node {n.index} bound a shared onion port "
                    "(127.0.0.1:28762 or the retired Bitcoin port 127.0.0.1:18445)"
                )
        genesis_time = self.nodes[0].getblockheader(self.nodes[0].getblockhash(0))["time"]
        self.mocktime = genesis_time + 60
        for n in self.nodes:
            n.setmocktime(self.mocktime)

    def _startup_failure_detail(self, node):
        lines = []
        try:
            text = node.debug_log_path.read_text(encoding="utf-8", errors="replace")
        except OSError as exc:
            return f"debug log unreadable: {exc}"
        for line in text.splitlines():
            if "bind" in line.lower() or "Failed to listen" in line or "Error:" in line:
                lines.append(line)
        return "\n".join(lines[-30:])

    def work(self, node):
        return int(node.getblockchaininfo()["chainwork"], 16)

    def run_test(self):
        n0, n1, ctl = self.nodes
        assert_equal(self.work(n0), GENESIS_WORK)

        self.log.info("Genesis-work gate: genesis-only node is not in IBD and serves headers")
        assert not n0.getblockchaininfo()["initialblockdownload"]
        assert not n1.getblockchaininfo()["initialblockdownload"]
        self.log.info("Control gate: genesis-only node stays in IBD")
        assert ctl.getblockchaininfo()["initialblockdownload"]

        self.log.info("Block 1 relays to the genesis-work peer; the control ignores heights below its gate")
        with ctl.assert_debug_log(expected_msgs=["Ignoring low-work chain"], timeout=10):
            for h in range(1, 4):
                self.mocktime += 600
                for n in self.nodes:
                    n.setmocktime(self.mocktime)
                self.generatetoaddress(n0, 1, ADDRESS_BCRT1_UNSPENDABLE, sync_fun=self.no_op)
                self.sync_blocks([n0, n1])
                assert_equal(n1.getblockcount(), h)
        assert_equal(ctl.getblockcount(), 0)
        assert_equal(ctl.getblockchaininfo()["headers"], 0)

        self.log.info("A node below its gate answers getheaders with an empty headers message")
        with ctl.assert_debug_log(expected_msgs=["because active chain has too little work; sending empty response"], timeout=10):
            self.disconnect_nodes(2, 0)
            self.connect_nodes(0, 2)

        self.log.info("Once the chain carries the control's gate work, the control syncs")
        self.mocktime += 600
        for n in self.nodes:
            n.setmocktime(self.mocktime)
        self.generatetoaddress(n0, 1, ADDRESS_BCRT1_UNSPENDABLE, sync_fun=self.no_op)
        assert_equal(self.work(n0), CONTROL_GATE)
        self.sync_blocks()
        assert_equal(ctl.getblockcount(), 4)


if __name__ == '__main__':
    MinChainWorkLaunchTest().main()
