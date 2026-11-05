// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INDEX_ECONOMICS_SNAPSHOT_H
#define BITCOIN_INDEX_ECONOMICS_SNAPSHOT_H

#include <index/economics_account.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

/** Wallet and RPC refresh interval. A snapshot younger than this is reused. */
inline constexpr std::chrono::seconds ECONOMICS_REFRESH_INTERVAL{300};

enum class EconomicsStatus {
    Disabled,
    InitialBlockDownload,
    Rebuilding,
    Empty,
    Unavailable,
    Corrupt,
    Busy,
    Stale,
    RefreshFailed,
    Lagging,
    Ok,
};

enum class EconomicsServe {
    MissingIndex,
    InitialBlockDownload,
    CatchingUp,
    Empty,
    OffChain,
    Serve,
};

[[nodiscard]] EconomicsServe DecideEconomicsServe(bool index_present, bool initial_block_download, bool synced, bool has_summary, bool on_active_chain);

struct EconomicsAmounts {
    bool subsidy_available{false};
    CAmount subsidy{0};
    bool convergence_available{false};
    CAmount convergence{0};
    bool plain_available{false};
    CAmount plain{0};
    bool mint_oru_available{false};
    CAmount mint_oru{0};
    bool mint_res_available{false};
    CAmount mint_res{0};
    bool fee_burn_available{false};
    CAmount fee_burn{0};
    bool total_destroyed_available{false};
    CAmount total_destroyed{0};
    bool oru_utxo_available{false};
    CAmount oru_utxo{0};
    bool res_generated_available{false};
    CAmount res_generated{0};
    bool res_destroyed_available{false};
    CAmount res_destroyed{0};
    bool res_supply_available{false};
    CAmount res_supply{0};
    bool ratio_available{false};
    CAmount historical_oru_per_res{0};
    bool destroy_cost_available{false};
    CAmount destroy_cost{0};
    CAmount res_cap{0};
};

struct EconomicsSnapshot {
    EconomicsStatus status{EconomicsStatus::Unavailable};
    bool amounts_authoritative{false};
    bool refresh_failed{false};
    int64_t generated_at{0};
    int indexed_height{-1};
    uint256 indexed_hash;
    int active_height{-1};
    int lag_blocks{-1};
    bool fee_history_known{false};
    bool res_supply_known{false};
    std::string index_state;
    EconomicsAmounts amounts;
};

/** Figures for one committed summary. Does not read the chain or the UTXO set. */
[[nodiscard]] EconomicsSnapshot BuildEconomicsSnapshot(const EconomicsBlockSummary& summary, int active_height, int64_t wall_time);

/**
 * One in-flight generation and one retained success.
 * A failed generation leaves the previous success in place.
 * A retained success whose hash is not the committed hash is not returned as current.
 */
class EconomicsSnapshotCache
{
public:
    EconomicsSnapshot Request(bool manual,
                              std::chrono::steady_clock::time_point now,
                              const uint256& committed_hash,
                              bool committed_on_chain,
                              const std::function<std::optional<EconomicsSnapshot>()>& produce);

private:
    mutable std::mutex m_mutex;
    bool m_in_flight{false};
    bool m_have_last{false};
    std::chrono::steady_clock::time_point m_last_at{};
    EconomicsSnapshot m_last;
};

[[nodiscard]] const char* EconomicsStatusName(EconomicsStatus status);

#endif // BITCOIN_INDEX_ECONOMICS_SNAPSHOT_H
