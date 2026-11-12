// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NET_PROCESSING_H
#define BITCOIN_NET_PROCESSING_H

#include <net.h>
#include <validationinterface.h>

class AddrMan;
class CChainParams;
class CTxMemPool;
class ChainstateManager;

/** Historical unique-tx cap used by tests/fuzz. Live `-maxorphantx` default is 0. */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** Default number of orphan+recently-replaced txn to keep around for block reconstruction */
static const unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;
static const bool DEFAULT_PEERBLOCKFILTERS = false;
/** Threshold for marking a node to be discouraged, e.g. disconnected and added to the discouragement filter. */
static const int DISCOURAGEMENT_THRESHOLD{100};
/** Maximum number of outstanding CMPCTBLOCK requests for the same block (v25.1 stall mitigation). */
static const unsigned int MAX_CMPCTBLOCKS_INFLIGHT_PER_BLOCK = 3;
/** Maximum concurrent compact-block reconstructions requested from one peer. */
static const unsigned int MAX_PARALLEL_COMPACT_REQUESTS_PER_PEER = 3;
/** Replaceable transaction announcements retained for one send-paused peer.
 *  This is INVENTORY_BROADCAST_MAX; net_processing.cpp static_asserts that.
 */
static constexpr unsigned int PAUSED_TX_INVENTORY_LIMIT = 35;

struct CNodeStateStats {
    int nSyncHeight = -1;
    int nCommonHeight = -1;
    int m_starting_height = -1;
    std::chrono::microseconds m_ping_wait;
    std::vector<int> vHeightInFlight;
    bool m_relay_txs;
    CAmount m_fee_filter_received;
    uint64_t m_addr_processed = 0;
    uint64_t m_addr_rate_limited = 0;
    bool m_addr_relay_enabled{false};
    ServiceFlags their_services;
    int64_t presync_height{-1};
    uint64_t m_enabled_capabilities{0};
    uint32_t m_rigor_protocol_version{0};
    bool m_capabilities_advertised{false};
};

class PeerManager : public CValidationInterface, public NetEventsInterface
{
public:
    static std::unique_ptr<PeerManager> make(CConnman& connman, AddrMan& addrman,
                                             BanMan* banman, ChainstateManager& chainman,
                                             CTxMemPool& pool, bool ignore_incoming_txs);
    virtual ~PeerManager() { }

    /**
     * Attempt to manually fetch block from a given peer. We must already have the header.
     *
     * @param[in]  peer_id      The peer id
     * @param[in]  block_index  The blockindex
     * @returns std::nullopt if a request was successfully made, otherwise an error message
     */
    virtual std::optional<std::string> FetchBlock(NodeId peer_id, const CBlockIndex& block_index) = 0;

    /** Begin running background tasks, should only be called once */
    virtual void StartScheduledTasks(CScheduler& scheduler) = 0;

    /** Get statistics from node state */
    virtual bool GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats) const = 0;

    /** Whether this node ignores txs received over p2p. */
    virtual bool IgnoresIncomingTxs() = 0;

    /** Relay transaction to all peers. */
    virtual void RelayTransaction(const uint256& txid, const uint256& wtxid) = 0;

    /** Queued transaction announcement hashes for one peer. Public for tests. */
    virtual size_t GetTxAnnouncementCount(NodeId nodeid) = 0;

    /** Queued block announcement hashes for one peer. Public for tests. */
    virtual size_t GetBlockAnnouncementCount(NodeId nodeid) = 0;

    /** Newest queued header-announcement hash, or null if that queue is empty. */
    virtual uint256 GetNewestBlockAnnouncement(NodeId nodeid) = 0;

    /** Unsent getblocks inventories for one peer. Not part of the announcement cap. */
    virtual size_t GetGetBlocksResponseCount(NodeId nodeid) = 0;

    /** Queue one tip announcement the same way a connected block does. Public for tests. */
    virtual void TestAnnounceBlockTip(const CBlockIndex* tip) = 0;

    /** Send ping message to all peers */
    virtual void SendPings() = 0;

    /** Set the best height */
    virtual void SetBestHeight(int height) = 0;

    /* Public for unit testing. */
    virtual void UnitTestMisbehaving(NodeId peer_id, int howmuch) = 0;

    /**
     * Evict extra outbound peers. If we think our tip may be stale, connect to an extra outbound.
     * Public for unit testing.
     */
    virtual void CheckForStaleTipAndEvictPeers() = 0;

    /** Process a single message from a peer. Public for fuzz testing */
    virtual void ProcessMessage(CNode& pfrom, const std::string& msg_type, CDataStream& vRecv,
                                const std::chrono::microseconds time_received, const std::atomic<bool>& interruptMsgProc) EXCLUSIVE_LOCKS_REQUIRED(g_msgproc_mutex) = 0;

    /** This function is used for testing the stale tip eviction logic, see denialofservice_tests.cpp */
    virtual void UpdateLastBlockAnnounceTime(NodeId node, int64_t time_in_seconds) = 0;
};

#endif // BITCOIN_NET_PROCESSING_H
