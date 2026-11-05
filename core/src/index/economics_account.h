// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_INDEX_ECONOMICS_ACCOUNT_H
#define BITCOIN_INDEX_ECONOMICS_ACCOUNT_H

#include <consensus/amount.h>
#include <serialize.h>
#include <uint256.h>

#include <cstdint>
#include <ios>
#include <optional>
#include <string>
#include <vector>

class CBlock;
class CBlockUndo;

/** One attribution row for a block. Deleted with the block hash. Not a global counter. */
struct EconomicsTxRow {
    uint256 txid;
    uint8_t kind{0}; //!< 1 mint-linked Convergence, 2 plain Convergence, 3 RES-transfer fee burn
    CAmount convergence{0};
    CAmount res_atoms{0};
    CAmount fee_burn{0};

    SERIALIZE_METHODS(EconomicsTxRow, obj)
    {
        READWRITE(obj.txid, obj.kind, obj.convergence, obj.res_atoms, obj.fee_burn);
    }
};

/**
 * One canonical block's economics summary.
 * Deltas are what this block did. Prefixes are the sum of deltas from genesis
 * through this hash, stored on the hash so a reorg deletes them instead of
 * subtracting from a lifetime key.
 */
struct EconomicsBlockSummary {
    static constexpr uint8_t SCHEMA{1};
    static constexpr uint8_t FEE_KNOWN{0};
    static constexpr uint8_t FEE_UNKNOWN{1};
    static constexpr uint8_t KIND_MINT{1};
    static constexpr uint8_t KIND_PLAIN{2};
    static constexpr uint8_t KIND_FEE{3};

    uint8_t schema{SCHEMA};
    int32_t height{-1};
    uint256 block_hash;
    uint256 prev_hash;
    CAmount subsidy{0};
    CAmount convergence{0};
    CAmount plain{0};
    CAmount mint_oru{0};
    CAmount mint_res{0};
    CAmount res_generated{0};
    uint32_t event_count{0};
    CAmount coinbase_convergence{0};
    uint8_t fee_state{FEE_KNOWN};
    CAmount fee_burn{0};
    bool fee_history_known{false};
    CAmount prefix_subsidy{0};
    CAmount prefix_convergence{0};
    CAmount prefix_plain{0};
    CAmount prefix_mint_oru{0};
    CAmount prefix_mint_res{0};
    CAmount prefix_res_generated{0};
    uint64_t prefix_event_count{0};
    CAmount prefix_fee_burn{0};
    int32_t last_mint_height{-1};
    bool oru_supply_known{false};
    bool res_supply_known{false};
    CAmount prefix_oru_utxo{0};
    CAmount prefix_res_destroyed{0};
    CAmount prefix_res_supply{0};

    SERIALIZE_METHODS(EconomicsBlockSummary, obj)
    {
        READWRITE(obj.schema);
        if (obj.schema != SCHEMA) {
            throw std::ios_base::failure("unsupported economics summary schema");
        }
        READWRITE(obj.height, obj.block_hash, obj.prev_hash, obj.subsidy, obj.convergence, obj.plain, obj.mint_oru, obj.mint_res, obj.res_generated, obj.event_count, obj.coinbase_convergence, obj.fee_state);
        if (obj.fee_state == FEE_KNOWN) READWRITE(obj.fee_burn);
        uint8_t fee_hist = obj.fee_history_known ? 1 : 0;
        uint8_t oru_known = obj.oru_supply_known ? 1 : 0;
        uint8_t res_known = obj.res_supply_known ? 1 : 0;
        READWRITE(fee_hist, obj.prefix_subsidy, obj.prefix_convergence, obj.prefix_plain, obj.prefix_mint_oru, obj.prefix_mint_res, obj.prefix_res_generated, obj.prefix_event_count);
        if (fee_hist != 0) READWRITE(obj.prefix_fee_burn);
        READWRITE(obj.last_mint_height, oru_known, res_known);
        if (oru_known != 0) READWRITE(obj.prefix_oru_utxo);
        if (res_known != 0) READWRITE(obj.prefix_res_destroyed, obj.prefix_res_supply);
        SER_READ(obj, obj.fee_history_known = fee_hist != 0);
        SER_READ(obj, obj.oru_supply_known = oru_known != 0);
        SER_READ(obj, obj.res_supply_known = res_known != 0);
    }
};

/** Fixed-point ratio with 8 decimal places. Empty when the denominator is not positive. */
[[nodiscard]] std::optional<CAmount> EconomicsFixedRatio8(CAmount numerator, CAmount denominator);

/**
 * Build the summary for one block from its bytes and, when present, its undo.
 * `undo == nullptr` records Convergence and RES generation and marks fees and
 * UTXO supply unknown. It does not store a zero fee or a zero supply.
 * A missing parent above genesis fails. Overflow fails. Neither path writes a substitute zero.
 */
[[nodiscard]] bool ApplyEconomicsBlock(const CBlock& block,
                                       int height,
                                       const uint256& block_hash,
                                       const uint256& prev_hash,
                                       const CBlockUndo* undo,
                                       const EconomicsBlockSummary* parent,
                                       EconomicsBlockSummary& out,
                                       std::vector<EconomicsTxRow>& rows,
                                       std::string& error);

#endif // BITCOIN_INDEX_ECONOMICS_ACCOUNT_H
