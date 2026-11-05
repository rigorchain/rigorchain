// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <core_io.h>
#include <index/economicsindex.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <univalue.h>

static UniValue AmountOrNull(bool available, CAmount amount)
{
    if (!available) return NullUniValue;
    return ValueFromAmount(amount);
}

static RPCHelpMan geteconomics()
{
    return RPCHelpMan{"geteconomics",
        "\nRead the local economics index. Amounts come from this node only.\n"
        "Current RES supply is not historical RES generation.\n"
        "ORU in the UTXO set is not the subsidy schedule and not Convergence destruction.\n"
        "The historical mint ratio and the next-block destroy cost are not market prices.\n"
        "A result is reused for 300 seconds unless \"manual\" is true.\n",
        {
            {"manual", RPCArg::Type::BOOL, RPCArg::Optional::OMITTED, "Skip the 300-second reuse and read the committed summary again."},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR, "status", "disabled, initial_block_download, rebuilding, empty, unavailable, corrupt, busy, stale, refresh_failed, lagging, or ok"},
                {RPCResult::Type::BOOL, "amounts_authoritative", "False when numbers must not be treated as final"},
                {RPCResult::Type::BOOL, "refresh_failed", "True when the previous successful figures were kept"},
                {RPCResult::Type::STR, "index_state", "Index metadata state"},
                {RPCResult::Type::NUM, "indexed_height", "Height of the committed economics summary"},
                {RPCResult::Type::STR, "indexed_block", "Hash of that summary"},
                {RPCResult::Type::NUM, "active_height", "Active chain height, status only"},
                {RPCResult::Type::NUM, "lag_blocks", "Active height minus indexed height"},
                {RPCResult::Type::NUM, "generated_at", "Unix time of the successful snapshot"},
                {RPCResult::Type::STR, "oru_subsidy_schedule", "Cumulative block subsidy, or null"},
                {RPCResult::Type::STR, "oru_convergence_destroyed", "Cumulative OP_CONVERGE value, or null"},
                {RPCResult::Type::STR, "oru_plain_destroyed", "Subset of Convergence with no RES mint, or null"},
                {RPCResult::Type::STR, "oru_mint_linked_destroyed", "Convergence ORU that minted RES, or null"},
                {RPCResult::Type::STR, "oru_fee_burn_destroyed", "RFC-018 RES-transfer fee burn, or null when unknown"},
                {RPCResult::Type::STR, "oru_total_destroyed", "Convergence plus fee burn, or null when fees are unknown"},
                {RPCResult::Type::STR, "oru_utxo_supply", "ORU currently in the UTXO set, or null"},
                {RPCResult::Type::STR, "res_generated", "Historical RES generation, or null"},
                {RPCResult::Type::STR, "res_destroyed", "RES removed from the UTXO set, or null"},
                {RPCResult::Type::STR, "res_supply", "Current RES in the UTXO set, or null. Not res_generated."},
                {RPCResult::Type::STR, "res_cap", "RFC-020 mint cap. Not the current supply."},
                {RPCResult::Type::STR, "historical_oru_per_res", "Mint-linked ORU per 1 RES, 8 decimal places, or null"},
                {RPCResult::Type::STR, "next_block_destroy_cost", "Protocol ORU cost of 1 RES at the next height, or null. Not a market price."},
            }
        },
        RPCExamples{
            HelpExampleCli("geteconomics", "")
            + HelpExampleRpc("geteconomics", "true")
        },
        [&](const RPCHelpMan& self, const JSONRPCRequest& request) -> UniValue
{
    const bool manual = !request.params[0].isNull() && request.params[0].get_bool();
    const EconomicsSnapshot snap = QueryEconomicsSnapshot(manual);
    UniValue result(UniValue::VOBJ);
    result.pushKV("status", EconomicsStatusName(snap.status));
    result.pushKV("amounts_authoritative", snap.amounts_authoritative);
    result.pushKV("refresh_failed", snap.refresh_failed);
    result.pushKV("index_state", snap.index_state);
    result.pushKV("indexed_height", snap.indexed_height);
    result.pushKV("indexed_block", snap.indexed_hash.IsNull() ? "" : snap.indexed_hash.ToString());
    result.pushKV("active_height", snap.active_height);
    result.pushKV("lag_blocks", snap.lag_blocks);
    result.pushKV("generated_at", snap.generated_at);
    const EconomicsAmounts& a = snap.amounts;
    result.pushKV("oru_subsidy_schedule", AmountOrNull(snap.amounts_authoritative && a.subsidy_available, a.subsidy));
    result.pushKV("oru_convergence_destroyed", AmountOrNull(snap.amounts_authoritative && a.convergence_available, a.convergence));
    result.pushKV("oru_plain_destroyed", AmountOrNull(snap.amounts_authoritative && a.plain_available, a.plain));
    result.pushKV("oru_mint_linked_destroyed", AmountOrNull(snap.amounts_authoritative && a.mint_oru_available, a.mint_oru));
    result.pushKV("oru_fee_burn_destroyed", AmountOrNull(snap.amounts_authoritative && a.fee_burn_available, a.fee_burn));
    result.pushKV("oru_total_destroyed", AmountOrNull(snap.amounts_authoritative && a.total_destroyed_available, a.total_destroyed));
    result.pushKV("oru_utxo_supply", AmountOrNull(snap.amounts_authoritative && a.oru_utxo_available, a.oru_utxo));
    result.pushKV("res_generated", AmountOrNull(snap.amounts_authoritative && a.res_generated_available, a.res_generated));
    result.pushKV("res_destroyed", AmountOrNull(snap.amounts_authoritative && a.res_destroyed_available, a.res_destroyed));
    result.pushKV("res_supply", AmountOrNull(snap.amounts_authoritative && a.res_supply_available, a.res_supply));
    result.pushKV("res_cap", snap.amounts_authoritative ? ValueFromAmount(a.res_cap) : NullUniValue);
    result.pushKV("historical_oru_per_res", AmountOrNull(snap.amounts_authoritative && a.ratio_available, a.historical_oru_per_res));
    result.pushKV("next_block_destroy_cost", AmountOrNull(snap.amounts_authoritative && a.destroy_cost_available, a.destroy_cost));
    return result;
},
    };
}

void RegisterEconomicsRPCCommands(CRPCTable& t)
{
    static const CRPCCommand command{"blockchain", &geteconomics};
    t.appendCommand(command.name, &command);
}
