// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <index/economicsindex.h>

#include <chain.h>
#include <chainparams.h>
#include <index/economics_account.h>
#include <logging.h>
#include <node/blockstorage.h>
#include <undo.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>

#include <ios>

#include <utility>

namespace {

constexpr uint8_t DB_SUMMARY{'S'};
constexpr uint8_t DB_HEIGHT{'H'};
constexpr uint8_t DB_ROWS{'L'};
constexpr uint8_t DB_META{'M'};

constexpr uint8_t STATE_EMPTY{0};
constexpr uint8_t STATE_REBUILDING{1};
constexpr uint8_t STATE_SYNCED{2};
constexpr uint8_t STATE_INCOMPLETE{3};

struct DBHashKey {
    uint8_t prefix;
    uint256 hash;
    explicit DBHashKey(uint8_t prefix_in, const uint256& hash_in) : prefix(prefix_in), hash(hash_in) {}
    SERIALIZE_METHODS(DBHashKey, obj)
    {
        READWRITE(obj.prefix, obj.hash);
    }
};

struct DBHeightKey {
    uint8_t prefix{DB_HEIGHT};
    uint32_t height{0};
    explicit DBHeightKey(int height_in) : height(static_cast<uint32_t>(height_in)) {}
    template <typename Stream>
    void Serialize(Stream& s) const
    {
        ser_writedata8(s, prefix);
        ser_writedata32be(s, height);
    }
    template <typename Stream>
    void Unserialize(Stream& s)
    {
        const uint8_t got = ser_readdata8(s);
        if (got != DB_HEIGHT) throw std::ios_base::failure("economics height key");
        height = ser_readdata32be(s);
    }
};

struct DBMetaKey {
    uint8_t prefix{DB_META};
    SERIALIZE_METHODS(DBMetaKey, obj) { READWRITE(obj.prefix); }
};

struct EconomicsMeta {
    uint8_t schema{EconomicsBlockSummary::SCHEMA};
    uint8_t state{STATE_EMPTY};
    int32_t best_height{-1};
    uint256 best_hash;
    uint8_t fee_history_known{0};
    int32_t rebuild_height{-1};
    int64_t last_commit_time{0};
    uint64_t blocks_committed{0};

    SERIALIZE_METHODS(EconomicsMeta, obj)
    {
        READWRITE(obj.schema);
        if (obj.schema != EconomicsBlockSummary::SCHEMA) {
            throw std::ios_base::failure("unsupported economics metadata schema");
        }
        READWRITE(obj.state, obj.best_height, obj.best_hash, obj.fee_history_known, obj.rebuild_height, obj.last_commit_time, obj.blocks_committed);
    }
};

const char* StateName(uint8_t state)
{
    switch (state) {
    case STATE_EMPTY: return "empty";
    case STATE_REBUILDING: return "rebuilding";
    case STATE_SYNCED: return "synced";
    case STATE_INCOMPLETE: return "incomplete";
    default: return "corrupt";
    }
}

} // namespace

std::unique_ptr<EconomicsIndex> g_economics_index;

EconomicsIndex::EconomicsIndex(std::unique_ptr<interfaces::Chain> chain, size_t cache_size, bool memory, bool wipe)
    : BaseIndex(std::move(chain), "economics")
{
    const fs::path path = gArgs.GetDataDirNet() / "indexes" / "economics";
    fs::create_directories(path);
    m_db = std::make_unique<BaseIndex::DB>(path / "db", cache_size, memory, wipe);
}

std::optional<EconomicsBlockSummary> EconomicsIndex::ReadSummary(const uint256& block_hash) const
{
    EconomicsBlockSummary summary;
    if (!m_db->Read(DBHashKey(DB_SUMMARY, block_hash), summary)) return std::nullopt;
    if (summary.block_hash != block_hash) return std::nullopt;
    return summary;
}

std::optional<std::vector<EconomicsTxRow>> EconomicsIndex::ReadRows(const uint256& block_hash) const
{
    std::vector<EconomicsTxRow> rows;
    if (!m_db->Read(DBHashKey(DB_ROWS, block_hash), rows)) return std::nullopt;
    return rows;
}

bool EconomicsIndex::EraseHeight(CDBBatch& batch, int height)
{
    uint256 hash;
    if (!m_db->Read(DBHeightKey(height), hash)) return true;
    batch.Erase(DBHashKey(DB_SUMMARY, hash));
    batch.Erase(DBHashKey(DB_ROWS, hash));
    batch.Erase(DBHeightKey(height));
    return true;
}

bool EconomicsIndex::CustomInit(const std::optional<interfaces::BlockKey>& block)
{
    if (!block) return true;
    const auto summary = ReadSummary(block->hash);
    if (!summary || summary->height != block->height) {
        return error("%s: economics index is missing the summary for its locator tip", __func__);
    }
    {
        LOCK(m_tip_mutex);
        m_have_tip = true;
        m_tip_height = block->height;
        m_tip_hash = block->hash;
    }
    std::vector<std::pair<int, uint256>> stale;
    {
        LOCK(cs_main);
        for (int height = block->height + 1;; ++height) {
            uint256 hash;
            if (!m_db->Read(DBHeightKey(height), hash)) break;
            const CBlockIndex* active = m_chainstate->m_chain[height];
            if (active == nullptr || active->GetBlockHash() != hash) {
                stale.emplace_back(height, hash);
            }
        }
    }
    if (stale.empty()) return true;
    CDBBatch batch(*m_db);
    for (const auto& [height, hash] : stale) {
        batch.Erase(DBHashKey(DB_SUMMARY, hash));
        batch.Erase(DBHashKey(DB_ROWS, hash));
        batch.Erase(DBHeightKey(height));
    }
    return m_db->WriteBatch(batch);
}

bool EconomicsIndex::CustomAppend(const interfaces::BlockInfo& block)
{
    if (block.data == nullptr || block.height < 0) {
        return error("%s: economics append missing block data", __func__);
    }
    const uint256 prev = block.prev_hash != nullptr ? *block.prev_hash : uint256{};
    std::optional<EconomicsBlockSummary> parent;
    if (block.height > 0) {
        parent = ReadSummary(prev);
        if (!parent) {
            return error("%s: economics parent summary missing for %s", __func__, prev.ToString());
        }
    }

    const CBlockUndo* undo_ptr = nullptr;
    CBlockUndo undo;
    if (block.height > 0) {
        if (block.undo_data != nullptr) {
            undo_ptr = block.undo_data;
        } else {
            const CBlockIndex* pindex = WITH_LOCK(cs_main, return m_chainstate->m_blockman.LookupBlockIndex(block.hash));
            if (pindex != nullptr && node::UndoReadFromDisk(undo, pindex)) {
                undo_ptr = &undo;
            }
        }
    }

    EconomicsBlockSummary summary;
    std::vector<EconomicsTxRow> rows;
    std::string error_text;
    if (!ApplyEconomicsBlock(*block.data, block.height, block.hash, prev, undo_ptr, parent ? &*parent : nullptr, summary, rows, error_text)) {
        return error("%s: %s", __func__, error_text);
    }

    CDBBatch batch(*m_db);
    uint256 previous_height_hash;
    if (m_db->Read(DBHeightKey(block.height), previous_height_hash) && previous_height_hash != block.hash) {
        batch.Erase(DBHashKey(DB_SUMMARY, previous_height_hash));
        batch.Erase(DBHashKey(DB_ROWS, previous_height_hash));
    }
    batch.Write(DBHashKey(DB_SUMMARY, block.hash), summary);
    batch.Write(DBHashKey(DB_ROWS, block.hash), rows);
    batch.Write(DBHeightKey(block.height), block.hash);
    if (!m_db->WriteBatch(batch)) {
        return error("%s: economics batch write failed", __func__);
    }
    LOCK(m_tip_mutex);
    m_have_tip = true;
    m_tip_height = block.height;
    m_tip_hash = block.hash;
    return true;
}

bool EconomicsIndex::CustomCommit(CDBBatch& batch)
{
    EconomicsMeta meta;
    int tip_height{-1};
    uint256 tip_hash;
    bool have_tip{false};
    {
        LOCK(m_tip_mutex);
        have_tip = m_have_tip;
        tip_height = m_tip_height;
        tip_hash = m_tip_hash;
    }
    if (have_tip) {
        const auto summary = ReadSummary(tip_hash);
        if (!summary) {
            return error("%s: refusing to commit an economics tip with no summary", __func__);
        }
        meta.best_height = tip_height;
        meta.best_hash = tip_hash;
        meta.fee_history_known = summary->fee_history_known ? 1 : 0;
        meta.rebuild_height = tip_height;
        meta.blocks_committed = static_cast<uint64_t>(tip_height) + 1;
        meta.state = GetSummary().synced ? (summary->fee_history_known ? STATE_SYNCED : STATE_INCOMPLETE) : STATE_REBUILDING;
    }
    meta.last_commit_time = GetTime();
    batch.Write(DBMetaKey{}, meta);
    return true;
}

bool EconomicsIndex::CustomRewind(const interfaces::BlockKey& current_tip, const interfaces::BlockKey& new_tip)
{
    CDBBatch batch(*m_db);
    for (int height = current_tip.height; height > new_tip.height; --height) {
        EraseHeight(batch, height);
    }
    if (!m_db->WriteBatch(batch)) return false;
    LOCK(m_tip_mutex);
    m_have_tip = true;
    m_tip_height = new_tip.height;
    m_tip_hash = new_tip.hash;
    return true;
}

EconomicsSnapshot EconomicsIndex::Query(bool manual)
{
    EconomicsSnapshot snap;
    if (m_chainstate == nullptr) {
        snap.status = EconomicsStatus::Rebuilding;
        snap.index_state = "rebuilding";
        return snap;
    }
    const bool initial_download = m_chainstate->IsInitialBlockDownload();
    bool synced = GetSummary().synced;
    // BlockConnected already records each new block. The on-disk meta pointer
    // moves only when the chainstate is fully flushed, which is not the
    // 300-second refresh. Drain queued blocks, then read the in-memory tip.
    if (synced) {
        BlockUntilSyncedToCurrentChain();
    }
    uint256 live_hash;
    int live_height{-1};
    bool have_live{false};
    if (synced) {
        LOCK(m_tip_mutex);
        if (m_have_tip && m_tip_height >= 0 && !m_tip_hash.IsNull()) {
            live_hash = m_tip_hash;
            live_height = m_tip_height;
            have_live = true;
        }
    }
    EconomicsMeta meta;
    const bool has_meta = m_db->Read(DBMetaKey{}, meta) && meta.schema == EconomicsBlockSummary::SCHEMA && meta.best_height >= 0 && !meta.best_hash.IsNull();
    const bool has_serve = have_live || has_meta;
    const uint256 serve_hash = have_live ? live_hash : (has_meta ? meta.best_hash : uint256{});
    const int serve_height = have_live ? live_height : (has_meta ? meta.best_height : GetSummary().best_block_height);
    bool on_chain = false;
    {
        LOCK(cs_main);
        snap.active_height = m_chainstate->m_chain.Height();
        if (has_serve && !serve_hash.IsNull()) {
            const CBlockIndex* indexed = m_chainstate->m_blockman.LookupBlockIndex(serve_hash);
            on_chain = indexed != nullptr && m_chainstate->m_chain.Contains(indexed);
        }
    }
    snap.indexed_height = serve_height;
    snap.index_state = has_meta ? StateName(meta.state) : (have_live ? "synced" : "empty");
    const EconomicsServe decision = DecideEconomicsServe(true, initial_download, synced, has_serve, on_chain);
    if (decision != EconomicsServe::Serve) {
        switch (decision) {
        case EconomicsServe::InitialBlockDownload: snap.status = EconomicsStatus::InitialBlockDownload; break;
        case EconomicsServe::CatchingUp: snap.status = EconomicsStatus::Rebuilding; break;
        case EconomicsServe::Empty: snap.status = EconomicsStatus::Empty; break;
        case EconomicsServe::OffChain: snap.status = EconomicsStatus::Stale; break;
        default: snap.status = EconomicsStatus::Unavailable; break;
        }
        return snap;
    }

    const int64_t wall = GetTime();
    const uint256 committed = serve_hash;
    return m_cache.Request(manual, std::chrono::steady_clock::now(), committed, true, [&]() -> std::optional<EconomicsSnapshot> {
        const auto summary = ReadSummary(committed);
        if (!summary) return std::nullopt;
        bool parent_ok = true;
        int active_height = -1;
        {
            LOCK(cs_main);
            active_height = m_chainstate->m_chain.Height();
            const CBlockIndex* indexed = m_chainstate->m_blockman.LookupBlockIndex(summary->block_hash);
            parent_ok = indexed != nullptr && m_chainstate->m_chain.Contains(indexed) &&
                        (summary->height == 0 || (indexed->pprev != nullptr && indexed->pprev->GetBlockHash() == summary->prev_hash));
        }
        if (!parent_ok) return std::nullopt;
        EconomicsSnapshot built = BuildEconomicsSnapshot(*summary, active_height, wall);
        built.index_state = summary->fee_history_known ? "synced" : "incomplete";
        if (!built.amounts.subsidy_available) {
            built.amounts_authoritative = false;
            built.status = EconomicsStatus::Unavailable;
            return std::nullopt;
        }
        return built;
    });
}

EconomicsSnapshot QueryEconomicsSnapshot(bool manual)
{
    if (!g_economics_index) {
        EconomicsSnapshot snap;
        snap.status = EconomicsStatus::Disabled;
        snap.index_state = "disabled";
        return snap;
    }
    return g_economics_index->Query(manual);
}
