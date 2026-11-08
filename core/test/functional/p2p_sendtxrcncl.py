#!/usr/bin/env python3
# Copyright (c) 2022 The Bitcoin Core developers
# Copyright (c) 2026 The RigorChain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Bitcoin SENDTXRCNCL negotiation is removed. CAP_TX_SYNC is not local-active."""

from test_framework.messages import (
    msg_sendtxrcncl,
)
from test_framework.p2p import P2PInterface
from test_framework.test_framework import BitcoinTestFramework


def create_sendtxrcncl_msg():
    sendtxrcncl_msg = msg_sendtxrcncl()
    sendtxrcncl_msg.version = 1
    sendtxrcncl_msg.salt = 2
    return sendtxrcncl_msg


class SendTxrcnclReceiver(P2PInterface):
    def __init__(self):
        super().__init__()
        self.sendtxrcncl_msg_received = None

    def on_sendtxrcncl(self, message):
        self.sendtxrcncl_msg_received = message


class SendTxRcnclTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [['-txreconciliation']]

    def run_test(self):
        self.log.info('Reserved -txreconciliation flag does not enable BIP330 relay')
        debug_log = self.nodes[0].debug_log_path.read_text(encoding='utf-8')
        assert "BIP330 set reconciliation is not implemented; transaction announcements remain on INV. Retaining the local transaction reconciliation tracker for future salt-transfer support only." in debug_log

        self.log.info('Node does not send SENDTXRCNCL (capability-driven handshake only)')
        peer = self.nodes[0].add_p2p_connection(SendTxrcnclReceiver())
        assert peer.sendtxrcncl_msg_received is None
        self.nodes[0].disconnect_p2ps()

        self.log.info('Inbound SENDTXRCNCL disconnects as obsolete Bitcoin negotiation')
        peer = self.nodes[0].add_p2p_connection(P2PInterface())
        with self.nodes[0].assert_debug_log(["obsolete bitcoin negotiation command"]):
            peer.send_message(create_sendtxrcncl_msg())
            peer.wait_for_disconnect()


if __name__ == '__main__':
    SendTxRcnclTest().main()
