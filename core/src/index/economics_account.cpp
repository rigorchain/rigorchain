// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <index/economics_account.h>

#include <chainparams.h>
#include <consensus/convergenceres.h>
#include <consensus/rigor_consensus_lock.h>
#include <consensus/rigor_issuance.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <undo.h>

#include <limits>

namespace {

bool AddInt64(CAmount& acc, CAmount delta)
{
    if (delta > 0) {
        if (acc > std::numeric_limits<CAmount>::max() - delta) return false;
    } else if (delta < 0) {
        if (acc < std::numeric_limits<CAmount>::min() - delta) return false;
    }
    acc += delta;
    return true;
}

bool AddNonNegative(CAmount& acc, CAmount delta)
{
    if (delta < 0) return false;
    return AddInt64(acc, delta);
}

void AddOutput(const CTxOut& out, CAmount& oru_enter, CAmount& res_enter, bool& ok)
{
    if (!ok || !EntersUtxoSet(out.scriptPubKey)) return;
    const AssetType asset = AssetTypeFromScriptPubKey(out.scriptPubKey);
    if (asset == ASSET_ORU) {
        ok = AddNonNegative(oru_enter, out.nValue);
    } else if (asset == ASSET_RES) {
        ok = AddNonNegative(res_enter, out.nValue);
    }
}

} // namespace

std::optional<CAmount> EconomicsFixedRatio8(CAmount numerator, CAmount denominator)
{
    if (denominator <= 0 || numerator < 0) return std::nullopt;
    const __int128 num = static_cast<__int128>(numerator) * static_cast<__int128>(COIN) +
                         static_cast<__int128>(denominator) / 2;
    const __int128 quot = num / static_cast<__int128>(denominator);
    if (quot > static_cast<__int128>(std::numeric_limits<CAmount>::max())) return std::nullopt;
    return static_cast<CAmount>(quot);
}

bool ApplyEconomicsBlock(const CBlock& block,
                         int height,
                         const uint256& block_hash,
                         const uint256& prev_hash,
                         const CBlockUndo* undo,
                         const EconomicsBlockSummary* parent,
                         EconomicsBlockSummary& out,
                         std::vector<EconomicsTxRow>& rows,
                         std::string& error)
{
    out = {};
    rows.clear();
    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase()) {
        error = "economics block has no coinbase";
        return false;
    }
    if (height < 0) {
        error = "economics block height is negative";
        return false;
    }
    if (height == 0) {
        if (parent != nullptr) {
            error = "genesis economics summary has a parent";
            return false;
        }
    } else if (parent == nullptr || parent->height + 1 != height || parent->block_hash != prev_hash) {
        error = "economics parent summary is missing or does not match prev_hash";
        return false;
    } else if (parent->schema != EconomicsBlockSummary::SCHEMA) {
        error = "economics parent schema is not supported";
        return false;
    }

    bool inputs_known = height == 0;
    if (height > 0 && undo != nullptr && undo->vtxundo.size() + 1 == block.vtx.size()) {
        inputs_known = true;
        for (size_t i = 1; i < block.vtx.size() && inputs_known; ++i) {
            const CTxUndo& tx_undo = undo->vtxundo[i - 1];
            const CTransaction& tx = *block.vtx[i];
            if (tx_undo.vprevout.size() != tx.vin.size()) {
                inputs_known = false;
                break;
            }
            for (const Coin& coin : tx_undo.vprevout) {
                if (coin.IsSpent()) {
                    inputs_known = false;
                    break;
                }
                const AssetType asset = AssetTypeFromScriptPubKey(coin.out.scriptPubKey);
                if (asset != ASSET_ORU && asset != ASSET_RES) {
                    inputs_known = false;
                    break;
                }
            }
        }
    }

    CAmount oru_in{0};
    CAmount res_in{0};
    CAmount oru_enter{0};
    CAmount res_enter{0};
    CAmount res_destroyed{0};
    CAmount extra_coinbase_res{0};
    bool fee_negative{false};
    bool arithmetic_ok{true};

    for (size_t i = 0; i < block.vtx.size(); ++i) {
        const CTransaction& tx = *block.vtx[i];
        const CAmount conv = Consensus::GetConvergenceAmount(tx);
        const CAmount res_out = tx.GetValueOut(ASSET_RES);
        const CAmount oru_out = tx.GetValueOut(ASSET_ORU);
        CAmount tx_oru_in{0};
        CAmount tx_res_in{0};
        if (inputs_known && !tx.IsCoinBase()) {
            for (const Coin& coin : undo->vtxundo[i - 1].vprevout) {
                const AssetType asset = AssetTypeFromScriptPubKey(coin.out.scriptPubKey);
                if (asset == ASSET_ORU) {
                    arithmetic_ok = arithmetic_ok && AddNonNegative(tx_oru_in, coin.out.nValue);
                } else if (asset == ASSET_RES) {
                    arithmetic_ok = arithmetic_ok && AddNonNegative(tx_res_in, coin.out.nValue);
                }
            }
            arithmetic_ok = arithmetic_ok && AddNonNegative(oru_in, tx_oru_in);
            arithmetic_ok = arithmetic_ok && AddNonNegative(res_in, tx_res_in);
        }

        if (conv > 0) {
            if (out.event_count == std::numeric_limits<uint32_t>::max()) {
                error = "economics event count overflow";
                return false;
            }
            ++out.event_count;
            arithmetic_ok = arithmetic_ok && AddNonNegative(out.convergence, conv);
            if (tx.IsCoinBase()) {
                arithmetic_ok = arithmetic_ok && AddNonNegative(out.coinbase_convergence, conv);
            }
            // A Convergence output forces ORU-only inputs, so RES on that transaction is a mint
            // even when undo is missing. A missing undo must not turn that RES into a fake zero burn.
            const bool mint = res_out > 0 && (inputs_known ? tx_res_in == 0 : true);
            EconomicsTxRow row;
            row.txid = tx.GetHash();
            row.convergence = conv;
            if (mint) {
                row.kind = EconomicsBlockSummary::KIND_MINT;
                row.res_atoms = res_out;
                arithmetic_ok = arithmetic_ok && AddNonNegative(out.mint_oru, conv);
                arithmetic_ok = arithmetic_ok && AddNonNegative(out.mint_res, res_out);
            } else {
                row.kind = EconomicsBlockSummary::KIND_PLAIN;
                arithmetic_ok = arithmetic_ok && AddNonNegative(out.plain, conv);
            }
            rows.push_back(row);
        }

        if (tx.IsCoinBase()) {
            const bool coinbase_mint = conv > 0 && res_out > 0;
            if (!coinbase_mint) {
                arithmetic_ok = arithmetic_ok && AddNonNegative(extra_coinbase_res, res_out);
            }
        } else if (inputs_known && tx_res_in > 0) {
            if (tx_res_in < res_out) {
                // RES appeared without a mint. Refuse rather than publish a supply.
                error = "economics RES output exceeds RES inputs without a mint";
                return false;
            }
            const CAmount destroyed = tx_res_in - res_out;
            arithmetic_ok = arithmetic_ok && AddNonNegative(res_destroyed, destroyed);
            if (tx_oru_in < oru_out) {
                fee_negative = true;
            } else {
                const CAmount burn = Consensus::OruResTransferFeeBurnShare(tx_oru_in - oru_out);
                arithmetic_ok = arithmetic_ok && AddNonNegative(out.fee_burn, burn);
                if (burn > 0 || destroyed > 0) {
                    EconomicsTxRow row;
                    row.txid = tx.GetHash();
                    row.kind = EconomicsBlockSummary::KIND_FEE;
                    row.fee_burn = burn;
                    row.res_atoms = destroyed;
                    rows.push_back(row);
                }
            }
        }

        if (inputs_known || tx.IsCoinBase()) {
            for (const CTxOut& txout : tx.vout) {
                AddOutput(txout, oru_enter, res_enter, arithmetic_ok);
            }
        }
    }

    if (!arithmetic_ok) {
        error = "economics amount overflow";
        return false;
    }
    if (out.plain + out.mint_oru != out.convergence || out.convergence < out.coinbase_convergence) {
        error = "economics Convergence split does not match";
        return false;
    }

    out.schema = EconomicsBlockSummary::SCHEMA;
    out.height = height;
    out.block_hash = block_hash;
    out.prev_hash = prev_hash;
    out.subsidy = GetBlockSubsidy(height, Params().GetConsensus());
    if (out.subsidy < 0) {
        error = "economics subsidy is negative";
        return false;
    }
    arithmetic_ok = AddNonNegative(out.res_generated, out.mint_res) &&
                    AddNonNegative(out.res_generated, extra_coinbase_res);
    if (!arithmetic_ok) {
        error = "economics RES generation overflow";
        return false;
    }

    const EconomicsBlockSummary zero_parent{};
    const EconomicsBlockSummary& base = parent != nullptr ? *parent : zero_parent;
    out.prefix_subsidy = base.prefix_subsidy;
    out.prefix_convergence = base.prefix_convergence;
    out.prefix_plain = base.prefix_plain;
    out.prefix_mint_oru = base.prefix_mint_oru;
    out.prefix_mint_res = base.prefix_mint_res;
    out.prefix_res_generated = base.prefix_res_generated;
    out.prefix_event_count = base.prefix_event_count;
    out.last_mint_height = out.res_generated > 0 ? height : base.last_mint_height;
    if (!AddNonNegative(out.prefix_subsidy, out.subsidy) ||
        !AddNonNegative(out.prefix_convergence, out.convergence) ||
        !AddNonNegative(out.prefix_plain, out.plain) ||
        !AddNonNegative(out.prefix_mint_oru, out.mint_oru) ||
        !AddNonNegative(out.prefix_mint_res, out.mint_res) ||
        !AddNonNegative(out.prefix_res_generated, out.res_generated) ||
        out.prefix_event_count > std::numeric_limits<uint64_t>::max() - out.event_count) {
        error = "economics prefix overflow";
        return false;
    }
    out.prefix_event_count += out.event_count;

    out.fee_state = (inputs_known && !fee_negative) ? EconomicsBlockSummary::FEE_KNOWN : EconomicsBlockSummary::FEE_UNKNOWN;
    if (out.fee_state != EconomicsBlockSummary::FEE_KNOWN) out.fee_burn = 0;
    out.fee_history_known = (parent == nullptr || parent->fee_history_known) && out.fee_state == EconomicsBlockSummary::FEE_KNOWN;
    if (out.fee_history_known) {
        out.prefix_fee_burn = base.prefix_fee_burn;
        if (!AddNonNegative(out.prefix_fee_burn, out.fee_burn)) {
            error = "economics fee prefix overflow";
            return false;
        }
    } else {
        out.prefix_fee_burn = 0;
    }

    out.oru_supply_known = inputs_known && (parent == nullptr || parent->oru_supply_known);
    out.res_supply_known = inputs_known && (parent == nullptr || parent->res_supply_known);
    if (out.oru_supply_known) {
        const CAmount delta = oru_enter - oru_in;
        out.prefix_oru_utxo = base.prefix_oru_utxo;
        if (!AddInt64(out.prefix_oru_utxo, delta) || out.prefix_oru_utxo < 0) {
            error = "economics ORU UTXO supply is inconsistent";
            return false;
        }
    }
    if (out.res_supply_known) {
        const CAmount delta = res_enter - res_in;
        const CAmount expected = out.res_generated - res_destroyed;
        if (delta != expected) {
            error = "economics RES supply delta does not match generation minus destruction";
            return false;
        }
        out.prefix_res_destroyed = base.prefix_res_destroyed;
        out.prefix_res_supply = base.prefix_res_supply;
        if (!AddNonNegative(out.prefix_res_destroyed, res_destroyed) ||
            !AddInt64(out.prefix_res_supply, delta) ||
            out.prefix_res_supply < 0 ||
            out.prefix_res_generated - out.prefix_res_destroyed != out.prefix_res_supply) {
            error = "economics RES supply prefix is inconsistent";
            return false;
        }
    } else {
        out.prefix_res_destroyed = 0;
        out.prefix_res_supply = 0;
        res_destroyed = 0;
    }
    if (!out.oru_supply_known) out.prefix_oru_utxo = 0;
    return true;
}
