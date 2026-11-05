// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INDEX_ECONOMICSINDEX_H
#define BITCOIN_INDEX_ECONOMICSINDEX_H

#include <index/base.h>
#include <index/economics_snapshot.h>
#include <sync.h>

#include <memory>

static constexpr bool DEFAULT_ECONOMICSINDEX{false};

class EconomicsIndex final : public BaseIndex
{
private:
    std::unique_ptr<BaseIndex::DB> m_db;
    EconomicsSnapshotCache m_cache;
    mutable Mutex m_tip_mutex;
    bool m_have_tip GUARDED_BY(m_tip_mutex){false};
    int m_tip_height GUARDED_BY(m_tip_mutex){-1};
    uint256 m_tip_hash GUARDED_BY(m_tip_mutex);

    bool AllowPrune() const override { return true; }

    bool CustomInit(const std::optional<interfaces::BlockKey>& block) override;
    bool CustomAppend(const interfaces::BlockInfo& block) override;
    bool CustomCommit(CDBBatch& batch) override;
    bool CustomRewind(const interfaces::BlockKey& current_tip, const interfaces::BlockKey& new_tip) override;
    BaseIndex::DB& GetDB() const override { return *m_db; }

    bool EraseHeight(CDBBatch& batch, int height);

public:
    explicit EconomicsIndex(std::unique_ptr<interfaces::Chain> chain, size_t cache_size, bool memory = false, bool wipe = false);

    std::optional<EconomicsBlockSummary> ReadSummary(const uint256& block_hash) const;
    std::optional<std::vector<EconomicsTxRow>> ReadRows(const uint256& block_hash) const;
    EconomicsSnapshot Query(bool manual);
};

extern std::unique_ptr<EconomicsIndex> g_economics_index;

[[nodiscard]] EconomicsSnapshot QueryEconomicsSnapshot(bool manual);

#endif // BITCOIN_INDEX_ECONOMICSINDEX_H
