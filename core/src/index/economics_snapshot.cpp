// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <index/economics_snapshot.h>

#include <chainparams.h>
#include <consensus/res_issuance.h>
#include <consensus/rigor_consensus_lock.h>
#include <node/supply_monitor/runtime_supply_state.h>

#include <limits>

EconomicsServe DecideEconomicsServe(bool index_present, bool initial_block_download, bool synced, bool has_summary, bool on_active_chain)
{
    if (!index_present) return EconomicsServe::MissingIndex;
    if (initial_block_download) return EconomicsServe::InitialBlockDownload;
    if (!synced) return EconomicsServe::CatchingUp;
    if (!has_summary) return EconomicsServe::Empty;
    if (!on_active_chain) return EconomicsServe::OffChain;
    return EconomicsServe::Serve;
}

const char* EconomicsStatusName(EconomicsStatus status)
{
    switch (status) {
    case EconomicsStatus::Disabled: return "disabled";
    case EconomicsStatus::InitialBlockDownload: return "initial_block_download";
    case EconomicsStatus::Rebuilding: return "rebuilding";
    case EconomicsStatus::Empty: return "empty";
    case EconomicsStatus::Unavailable: return "unavailable";
    case EconomicsStatus::Corrupt: return "corrupt";
    case EconomicsStatus::Busy: return "busy";
    case EconomicsStatus::Stale: return "stale";
    case EconomicsStatus::RefreshFailed: return "refresh_failed";
    case EconomicsStatus::Lagging: return "lagging";
    case EconomicsStatus::Ok: return "ok";
    }
    return "unavailable";
}

EconomicsSnapshot BuildEconomicsSnapshot(const EconomicsBlockSummary& summary, int active_height, int64_t wall_time)
{
    EconomicsSnapshot snap;
    snap.generated_at = wall_time;
    snap.indexed_height = summary.height;
    snap.indexed_hash = summary.block_hash;
    snap.active_height = active_height;
    snap.lag_blocks = active_height >= 0 && summary.height >= 0 ? active_height - summary.height : -1;
    snap.fee_history_known = summary.fee_history_known;
    snap.res_supply_known = summary.res_supply_known;
    snap.status = snap.lag_blocks > 0 ? EconomicsStatus::Lagging : EconomicsStatus::Ok;
    snap.amounts_authoritative = true;

    const CAmount derived = Consensus::ComputeDerivedOruSubsidyCumulative(summary.height, Params().GetConsensus());
    if (derived >= 0 && derived == summary.prefix_subsidy) {
        snap.amounts.subsidy_available = true;
        snap.amounts.subsidy = summary.prefix_subsidy;
    }
    snap.amounts.convergence_available = true;
    snap.amounts.convergence = summary.prefix_convergence;
    snap.amounts.plain_available = true;
    snap.amounts.plain = summary.prefix_plain;
    snap.amounts.mint_oru_available = true;
    snap.amounts.mint_oru = summary.prefix_mint_oru;
    snap.amounts.mint_res_available = true;
    snap.amounts.mint_res = summary.prefix_mint_res;
    snap.amounts.res_generated_available = true;
    snap.amounts.res_generated = summary.prefix_res_generated;
    snap.amounts.res_cap = Consensus::RES_MAX_SUPPLY;
    if (snap.amounts.res_generated >= 0 && snap.amounts.res_generated <= snap.amounts.res_cap) {
        snap.amounts.res_remaining_available = true;
        snap.amounts.res_remaining = snap.amounts.res_cap - snap.amounts.res_generated;
    }

    if (summary.fee_history_known) {
        snap.amounts.fee_burn_available = true;
        snap.amounts.fee_burn = summary.prefix_fee_burn;
        const __int128 total = static_cast<__int128>(summary.prefix_convergence) + summary.prefix_fee_burn;
        if (total >= 0 && total <= static_cast<__int128>(std::numeric_limits<CAmount>::max())) {
            snap.amounts.total_destroyed_available = true;
            snap.amounts.total_destroyed = static_cast<CAmount>(total);
        }
    }
    if (summary.oru_supply_known) {
        snap.amounts.oru_utxo_available = true;
        snap.amounts.oru_utxo = summary.prefix_oru_utxo;
    }
    if (summary.res_supply_known) {
        snap.amounts.res_destroyed_available = true;
        snap.amounts.res_destroyed = summary.prefix_res_destroyed;
        snap.amounts.res_supply_available = true;
        snap.amounts.res_supply = summary.prefix_res_supply;
    }
    if (const auto ratio = EconomicsFixedRatio8(summary.prefix_mint_oru, summary.prefix_mint_res)) {
        snap.amounts.ratio_available = true;
        snap.amounts.historical_oru_per_res = *ratio;
    }
    if (summary.height >= 0 && summary.height < std::numeric_limits<int>::max()) {
        const CAmount cost = Consensus::ResDestroyCostAtHeight(summary.height + 1);
        if (cost > 0) {
            snap.amounts.destroy_cost_available = true;
            snap.amounts.destroy_cost = cost;
        }
    }
    return snap;
}

EconomicsSnapshot EconomicsSnapshotCache::Request(bool manual,
                                                  std::chrono::steady_clock::time_point now,
                                                  const uint256& committed_hash,
                                                  bool committed_on_chain,
                                                  const std::function<std::optional<EconomicsSnapshot>()>& produce)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    const bool last_matches = m_have_last && m_last.indexed_hash == committed_hash && committed_on_chain && m_last.amounts_authoritative;
    if (m_in_flight) {
        EconomicsSnapshot busy;
        busy.status = EconomicsStatus::Busy;
        if (last_matches) {
            busy = m_last;
            busy.status = EconomicsStatus::Busy;
            busy.refresh_failed = false;
        }
        return busy;
    }
    if (!manual && last_matches && now < m_last_at + ECONOMICS_REFRESH_INTERVAL) {
        return m_last;
    }
    if (!committed_on_chain) {
        EconomicsSnapshot stale;
        stale.status = EconomicsStatus::Stale;
        return stale;
    }
    m_in_flight = true;
    lock.unlock();
    std::optional<EconomicsSnapshot> produced;
    try {
        produced = produce();
    } catch (...) {
        produced = std::nullopt;
    }
    lock.lock();
    m_in_flight = false;
    if (produced && produced->amounts_authoritative && produced->indexed_hash == committed_hash) {
        m_last = *produced;
        m_last_at = now;
        m_have_last = true;
        return m_last;
    }
    if (last_matches) {
        EconomicsSnapshot failed = m_last;
        failed.status = EconomicsStatus::RefreshFailed;
        failed.refresh_failed = true;
        return failed;
    }
    EconomicsSnapshot unavailable;
    unavailable.status = EconomicsStatus::Unavailable;
    return unavailable;
}
