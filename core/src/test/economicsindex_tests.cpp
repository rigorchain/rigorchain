// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/convergenceres.h>
#include <consensus/res_issuance.h>
#include <consensus/rigor_consensus_lock.h>
#include <index/economics_account.h>
#include <index/economicsindex.h>
#include <interfaces/chain.h>
#include <node/supply_monitor/runtime_supply_state.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <test/util/script.h>
#include <test/util/setup_common.h>
#include <tinyformat.h>
#include <undo.h>
#include <util/time.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>

namespace {

CScript OruScript()
{
    return CScript() << RIGOR_PUSH_0 << std::vector<unsigned char>(20, 0x11);
}

CScript ResScript()
{
    return TestNativeResScript();
}

CScript ConvergeScript()
{
    return Consensus::ConvergenceOutputScript();
}

CMutableTransaction MakeCoinbase(const std::vector<CTxOut>& outs)
{
    CMutableTransaction tx;
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << 1;
    tx.vout = outs;
    return tx;
}

CMutableTransaction MakeBody(const std::vector<CTxOut>& outs, size_t inputs)
{
    CMutableTransaction tx;
    tx.vin.resize(inputs);
    for (size_t i = 0; i < inputs; ++i) {
        tx.vin[i].prevout.hash = uint256S(strprintf("%02x", i + 1));
        tx.vin[i].prevout.n = 0;
    }
    tx.vout = outs;
    return tx;
}

CBlock MakeBlock(const std::vector<CMutableTransaction>& txs)
{
    CBlock block;
    for (const CMutableTransaction& tx : txs) {
        block.vtx.push_back(MakeTransactionRef(tx));
    }
    return block;
}

Coin MakeCoin(CAmount value, const CScript& script)
{
    return Coin(CTxOut(value, script), 1, false);
}

EconomicsBlockSummary ParentAt(int height, const uint256& hash)
{
    EconomicsBlockSummary parent;
    parent.schema = EconomicsBlockSummary::SCHEMA;
    parent.height = height;
    parent.block_hash = hash;
    parent.fee_history_known = true;
    parent.oru_supply_known = true;
    parent.res_supply_known = true;
    parent.fee_state = EconomicsBlockSummary::FEE_KNOWN;
    return parent;
}

void IndexWaitSynced(BaseIndex& index)
{
    const auto timeout = GetTime<std::chrono::seconds>() + 120s;
    while (!index.BlockUntilSyncedToCurrentChain()) {
        BOOST_REQUIRE(timeout > GetTime<std::chrono::milliseconds>());
        UninterruptibleSleep(100ms);
    }
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(economics_account_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(genesis_subsidy_is_zero_and_not_a_fake_supply)
{
    const CBlock block = MakeBlock({MakeCoinbase({CTxOut(0, OruScript())})});
    EconomicsBlockSummary summary;
    std::vector<EconomicsTxRow> rows;
    std::string error;
    BOOST_REQUIRE(ApplyEconomicsBlock(block, 0, uint256S("aa"), uint256{}, nullptr, nullptr, summary, rows, error));
    BOOST_CHECK_EQUAL(summary.subsidy, 0);
    BOOST_CHECK_EQUAL(summary.prefix_subsidy, 0);
    BOOST_CHECK(summary.fee_history_known);
    BOOST_CHECK_EQUAL(summary.fee_state, EconomicsBlockSummary::FEE_KNOWN);
    BOOST_CHECK_EQUAL(summary.fee_burn, 0);
    BOOST_CHECK(summary.oru_supply_known);
    BOOST_CHECK(summary.res_supply_known);
    BOOST_CHECK_EQUAL(summary.prefix_res_generated, 0);
    BOOST_CHECK_EQUAL(summary.prefix_res_supply, 0);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(0, Params().GetConsensus()), 0);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, Params().GetConsensus()), Consensus::ORU_FLAT_BLOCK_SUBSIDY);
    BOOST_CHECK_EQUAL(Consensus::ComputeDerivedOruSubsidyCumulative(0, Params().GetConsensus()), 0);
    BOOST_CHECK_EQUAL(Consensus::ComputeDerivedOruSubsidyCumulative(1, Params().GetConsensus()), Consensus::ORU_FLAT_BLOCK_SUBSIDY);
}

BOOST_AUTO_TEST_CASE(plain_mint_fee_and_res_supply_are_distinct)
{
    const uint256 genesis_hash = uint256S("aa");
    EconomicsBlockSummary genesis = ParentAt(-1, uint256{});
    genesis.height = -1;
    genesis.block_hash = uint256{};
    // Height 0 is built for real, then used as the parent.
    const CBlock genesis_block = MakeBlock({MakeCoinbase({CTxOut(0, OruScript())})});
    std::vector<EconomicsTxRow> rows;
    std::string error;
    BOOST_REQUIRE(ApplyEconomicsBlock(genesis_block, 0, genesis_hash, uint256{}, nullptr, nullptr, genesis, rows, error));

    CBlockUndo undo;
    CTxUndo spent;
    spent.vprevout.push_back(MakeCoin(1000, OruScript()));
    undo.vtxundo.push_back(spent);
    const CBlock plain = MakeBlock({
        MakeCoinbase({CTxOut(Consensus::ORU_FLAT_BLOCK_SUBSIDY, OruScript())}),
        MakeBody({CTxOut(1000, ConvergeScript())}, 1),
    });
    EconomicsBlockSummary plain_summary;
    BOOST_REQUIRE(ApplyEconomicsBlock(plain, 1, uint256S("bb"), genesis_hash, &undo, &genesis, plain_summary, rows, error));
    BOOST_CHECK_EQUAL(plain_summary.plain, 1000);
    BOOST_CHECK_EQUAL(plain_summary.mint_oru, 0);
    BOOST_CHECK_EQUAL(plain_summary.convergence, 1000);
    BOOST_CHECK_EQUAL(plain_summary.fee_burn, 0);
    BOOST_CHECK_EQUAL(plain_summary.prefix_subsidy, Consensus::ORU_FLAT_BLOCK_SUBSIDY);
    BOOST_CHECK(plain_summary.fee_history_known);

    CBlockUndo mint_undo;
    CTxUndo mint_spent;
    mint_spent.vprevout.push_back(MakeCoin(5000, OruScript()));
    mint_undo.vtxundo.push_back(mint_spent);
    const CAmount minted = 5 * COIN;
    const CBlock mint = MakeBlock({
        MakeCoinbase({CTxOut(Consensus::ORU_FLAT_BLOCK_SUBSIDY, OruScript())}),
        MakeBody({CTxOut(4000, ConvergeScript()), CTxOut(minted, ResScript())}, 1),
    });
    EconomicsBlockSummary mint_summary;
    BOOST_REQUIRE(ApplyEconomicsBlock(mint, 2, uint256S("cc"), uint256S("bb"), &mint_undo, &plain_summary, mint_summary, rows, error));
    BOOST_CHECK_EQUAL(mint_summary.mint_oru, 4000);
    BOOST_CHECK_EQUAL(mint_summary.mint_res, minted);
    BOOST_CHECK_EQUAL(mint_summary.plain, 0);
    BOOST_CHECK_EQUAL(mint_summary.fee_burn, 0);
    BOOST_CHECK_EQUAL(mint_summary.prefix_res_generated, minted);
    BOOST_CHECK_EQUAL(mint_summary.prefix_res_supply, minted);
    BOOST_CHECK_EQUAL(mint_summary.prefix_res_destroyed, 0);
    CAmount row_oru = 0;
    CAmount row_res = 0;
    for (const EconomicsTxRow& row : rows) {
        if (row.kind == EconomicsBlockSummary::KIND_MINT) {
            row_oru += row.convergence;
            row_res += row.res_atoms;
        }
    }
    BOOST_CHECK_EQUAL(row_oru, 4000);
    BOOST_CHECK_EQUAL(row_res, minted);

    CBlockUndo burn_undo;
    CTxUndo burn_spent;
    burn_spent.vprevout.push_back(MakeCoin(minted, ResScript()));
    burn_spent.vprevout.push_back(MakeCoin(100, OruScript()));
    burn_undo.vtxundo.push_back(burn_spent);
    const CBlock destroy = MakeBlock({
        MakeCoinbase({CTxOut(Consensus::ORU_FLAT_BLOCK_SUBSIDY, OruScript())}),
        MakeBody({CTxOut(minted - 2 * COIN, ResScript())}, 2),
    });
    EconomicsBlockSummary destroyed;
    BOOST_REQUIRE(ApplyEconomicsBlock(destroy, 3, uint256S("dd"), uint256S("cc"), &burn_undo, &mint_summary, destroyed, rows, error));
    BOOST_CHECK_EQUAL(destroyed.prefix_res_generated, minted);
    BOOST_CHECK_EQUAL(destroyed.prefix_res_destroyed, 2 * COIN);
    BOOST_CHECK_EQUAL(destroyed.prefix_res_supply, minted - 2 * COIN);
    BOOST_CHECK(destroyed.prefix_res_supply != destroyed.prefix_res_generated);
    BOOST_CHECK_EQUAL(destroyed.fee_burn, Consensus::OruResTransferFeeBurnShare(100));
    BOOST_CHECK_EQUAL(destroyed.fee_burn, 50);
    BOOST_CHECK(destroyed.fee_history_known);
    BOOST_CHECK(destroyed.prefix_fee_burn > 0);
    BOOST_CHECK_EQUAL(destroyed.prefix_convergence, plain_summary.prefix_convergence + mint_summary.convergence);

    const EconomicsSnapshot view = BuildEconomicsSnapshot(destroyed, 3, 1);
    BOOST_CHECK(view.amounts_authoritative);
    BOOST_CHECK(view.amounts.res_generated_available);
    BOOST_CHECK(view.amounts.res_supply_available);
    BOOST_CHECK_EQUAL(view.amounts.res_generated, minted);
    BOOST_CHECK_EQUAL(view.amounts.res_supply, minted - 2 * COIN);
    BOOST_CHECK(view.amounts.res_generated != view.amounts.res_supply);
    BOOST_CHECK(view.amounts.total_destroyed_available);
    BOOST_CHECK(view.amounts.total_destroyed != view.amounts.convergence);
    BOOST_CHECK(view.amounts.oru_utxo_available);
    BOOST_CHECK(view.amounts.oru_utxo != view.amounts.subsidy);
}

BOOST_AUTO_TEST_CASE(missing_undo_is_not_a_zero_fee_or_zero_supply)
{
    EconomicsBlockSummary genesis;
    std::vector<EconomicsTxRow> rows;
    std::string error;
    const CBlock genesis_block = MakeBlock({MakeCoinbase({CTxOut(0, OruScript())})});
    BOOST_REQUIRE(ApplyEconomicsBlock(genesis_block, 0, uint256S("aa"), uint256{}, nullptr, nullptr, genesis, rows, error));
    const CBlock body = MakeBlock({
        MakeCoinbase({CTxOut(Consensus::ORU_FLAT_BLOCK_SUBSIDY, OruScript())}),
        MakeBody({CTxOut(50, ConvergeScript()), CTxOut(COIN, ResScript())}, 1),
    });
    EconomicsBlockSummary summary;
    BOOST_REQUIRE(ApplyEconomicsBlock(body, 1, uint256S("bb"), uint256S("aa"), nullptr, &genesis, summary, rows, error));
    BOOST_CHECK_EQUAL(summary.fee_state, EconomicsBlockSummary::FEE_UNKNOWN);
    BOOST_CHECK(!summary.fee_history_known);
    BOOST_CHECK(!summary.oru_supply_known);
    BOOST_CHECK(!summary.res_supply_known);
    BOOST_CHECK_EQUAL(summary.prefix_fee_burn, 0);
    BOOST_CHECK_EQUAL(summary.mint_oru, 50);
    BOOST_CHECK_EQUAL(summary.mint_res, COIN);
    BOOST_CHECK_EQUAL(summary.prefix_res_generated, COIN);
    const EconomicsSnapshot view = BuildEconomicsSnapshot(summary, 1, 1);
    BOOST_CHECK(view.amounts.convergence_available);
    BOOST_CHECK(view.amounts.res_generated_available);
    BOOST_CHECK(!view.amounts.fee_burn_available);
    BOOST_CHECK(!view.amounts.total_destroyed_available);
    BOOST_CHECK(!view.amounts.res_supply_available);
    BOOST_CHECK(!view.amounts.oru_utxo_available);
    BOOST_CHECK_EQUAL(view.amounts.res_supply, 0);
}

BOOST_AUTO_TEST_CASE(one_atom_fee_burns_zero_and_is_known)
{
    EconomicsBlockSummary genesis;
    std::vector<EconomicsTxRow> rows;
    std::string error;
    BOOST_REQUIRE(ApplyEconomicsBlock(MakeBlock({MakeCoinbase({CTxOut(0, OruScript())})}), 0, uint256S("aa"), uint256{}, nullptr, nullptr, genesis, rows, error));
    CBlockUndo undo;
    CTxUndo spent;
    spent.vprevout.push_back(MakeCoin(COIN, ResScript()));
    spent.vprevout.push_back(MakeCoin(1, OruScript()));
    undo.vtxundo.push_back(spent);
    const CBlock block = MakeBlock({
        MakeCoinbase({CTxOut(Consensus::ORU_FLAT_BLOCK_SUBSIDY, OruScript())}),
        MakeBody({CTxOut(COIN, ResScript())}, 2),
    });
    EconomicsBlockSummary summary;
    BOOST_REQUIRE(ApplyEconomicsBlock(block, 1, uint256S("bb"), uint256S("aa"), &undo, &genesis, summary, rows, error));
    BOOST_CHECK_EQUAL(summary.fee_state, EconomicsBlockSummary::FEE_KNOWN);
    BOOST_CHECK_EQUAL(summary.fee_burn, 0);
    BOOST_CHECK(summary.fee_history_known);
    BOOST_CHECK_EQUAL(Consensus::OruResTransferFeeBurnShare(1), 0);
    BOOST_CHECK_EQUAL(summary.prefix_res_destroyed, 0);
    BOOST_CHECK_EQUAL(summary.prefix_res_supply, 0);
    BOOST_CHECK_EQUAL(summary.prefix_res_generated, 0);
}

BOOST_AUTO_TEST_CASE(coinbase_convergence_is_counted_and_genesis_parent_is_required)
{
    EconomicsBlockSummary genesis;
    std::vector<EconomicsTxRow> rows;
    std::string error;
    const CBlock genesis_block = MakeBlock({MakeCoinbase({CTxOut(7, ConvergeScript()), CTxOut(0, OruScript())})});
    BOOST_REQUIRE(ApplyEconomicsBlock(genesis_block, 0, uint256S("aa"), uint256{}, nullptr, nullptr, genesis, rows, error));
    BOOST_CHECK_EQUAL(genesis.coinbase_convergence, 7);
    BOOST_CHECK_EQUAL(genesis.convergence, 7);
    BOOST_CHECK_EQUAL(genesis.prefix_oru_utxo, 0);
    BOOST_CHECK(!ApplyEconomicsBlock(MakeBlock({MakeCoinbase({CTxOut(0, OruScript())})}), 1, uint256S("bb"), uint256S("aa"), nullptr, nullptr, genesis, rows, error));
}

BOOST_AUTO_TEST_CASE(subsidy_prefix_overflow_fails_closed)
{
    EconomicsBlockSummary parent = ParentAt(0, uint256S("aa"));
    parent.prefix_subsidy = std::numeric_limits<CAmount>::max() - 10;
    EconomicsBlockSummary summary;
    std::vector<EconomicsTxRow> rows;
    std::string error;
    const CBlock block = MakeBlock({MakeCoinbase({CTxOut(0, OruScript())})});
    BOOST_CHECK(!ApplyEconomicsBlock(block, 1, uint256S("bb"), uint256S("aa"), nullptr, &parent, summary, rows, error));
}

BOOST_AUTO_TEST_CASE(ratio_is_unavailable_when_no_res_was_minted)
{
    BOOST_CHECK(!EconomicsFixedRatio8(100, 0).has_value());
    BOOST_CHECK(!EconomicsFixedRatio8(-1, 1).has_value());
    const auto ratio = EconomicsFixedRatio8(2 * COIN, COIN);
    BOOST_REQUIRE(ratio.has_value());
    BOOST_CHECK_EQUAL(*ratio, 2 * COIN);
}

BOOST_AUTO_TEST_CASE(serve_decision_hides_incomplete_and_disconnected_data)
{
    BOOST_CHECK(DecideEconomicsServe(false, false, true, true, true) == EconomicsServe::MissingIndex);
    BOOST_CHECK(DecideEconomicsServe(true, true, true, true, true) == EconomicsServe::InitialBlockDownload);
    BOOST_CHECK(DecideEconomicsServe(true, false, false, true, true) == EconomicsServe::CatchingUp);
    BOOST_CHECK(DecideEconomicsServe(true, false, true, false, true) == EconomicsServe::Empty);
    BOOST_CHECK(DecideEconomicsServe(true, false, true, true, false) == EconomicsServe::OffChain);
    BOOST_CHECK(DecideEconomicsServe(true, false, true, true, true) == EconomicsServe::Serve);
}

BOOST_AUTO_TEST_CASE(snapshot_cache_is_single_flight_and_keeps_the_last_success)
{
    BOOST_CHECK_EQUAL(ECONOMICS_REFRESH_INTERVAL.count(), 300);
    EconomicsSnapshotCache cache;
    EconomicsSnapshot good;
    good.status = EconomicsStatus::Ok;
    good.amounts_authoritative = true;
    good.indexed_hash = uint256S("11");
    good.amounts.res_generated_available = true;
    good.amounts.res_generated = 5 * COIN;
    good.amounts.res_supply_available = true;
    good.amounts.res_supply = 2 * COIN;
    int calls = 0;
    const auto produce_ok = [&]() -> std::optional<EconomicsSnapshot> {
        ++calls;
        return good;
    };
    const auto t0 = std::chrono::steady_clock::time_point{};
    const EconomicsSnapshot first = cache.Request(false, t0, good.indexed_hash, true, produce_ok);
    BOOST_CHECK_EQUAL(calls, 1);
    BOOST_CHECK_EQUAL(first.amounts.res_supply, 2 * COIN);
    const EconomicsSnapshot cached = cache.Request(false, t0 + 299s, good.indexed_hash, true, produce_ok);
    BOOST_CHECK_EQUAL(calls, 1);
    BOOST_CHECK_EQUAL(cached.amounts.res_generated, first.amounts.res_generated);
    const EconomicsSnapshot due = cache.Request(false, t0 + ECONOMICS_REFRESH_INTERVAL, good.indexed_hash, true, produce_ok);
    BOOST_CHECK_EQUAL(calls, 2);
    BOOST_CHECK_EQUAL(due.amounts.res_supply, 2 * COIN);
    const EconomicsSnapshot manual = cache.Request(true, t0 + ECONOMICS_REFRESH_INTERVAL, good.indexed_hash, true, produce_ok);
    BOOST_CHECK_EQUAL(calls, 3);
    BOOST_CHECK(manual.amounts.res_supply != manual.amounts.res_generated);

    const auto fail = [&]() -> std::optional<EconomicsSnapshot> {
        ++calls;
        return std::nullopt;
    };
    const EconomicsSnapshot kept = cache.Request(true, t0 + 1000s, good.indexed_hash, true, fail);
    BOOST_CHECK(kept.refresh_failed);
    BOOST_CHECK_EQUAL(kept.amounts.res_supply, 2 * COIN);
    BOOST_CHECK_EQUAL(kept.indexed_hash, good.indexed_hash);

    const EconomicsSnapshot disconnected = cache.Request(true, t0 + 1000s, uint256S("22"), false, produce_ok);
    BOOST_CHECK(!disconnected.amounts_authoritative);
    BOOST_CHECK(disconnected.status == EconomicsStatus::Stale);

    std::mutex gate_mu;
    std::condition_variable gate;
    bool entered = false;
    bool release = false;
    EconomicsSnapshotCache overlap;
    const auto blocking = [&]() -> std::optional<EconomicsSnapshot> {
        std::unique_lock<std::mutex> lock(gate_mu);
        entered = true;
        gate.notify_all();
        gate.wait(lock, [&] { return release; });
        return good;
    };
    std::atomic<int> busy_status{-1};
    std::thread worker([&] {
        const EconomicsSnapshot result = overlap.Request(true, t0, good.indexed_hash, true, blocking);
        BOOST_CHECK_EQUAL(result.amounts.res_supply, 2 * COIN);
    });
    {
        std::unique_lock<std::mutex> lock(gate_mu);
        BOOST_REQUIRE(gate.wait_for(lock, 5s, [&] { return entered; }));
    }
    const EconomicsSnapshot busy = overlap.Request(true, t0, good.indexed_hash, true, produce_ok);
    busy_status = static_cast<int>(busy.status);
    {
        std::lock_guard<std::mutex> lock(gate_mu);
        release = true;
    }
    gate.notify_all();
    worker.join();
    BOOST_CHECK(busy.status == EconomicsStatus::Busy);
    BOOST_CHECK(!busy.amounts_authoritative);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(economics_index_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(index_sync_matches_subsidy_schedule_and_reorg_deletes_the_old_hash)
{
    EconomicsIndex index{interfaces::MakeChain(m_node), 1 << 20, true};
    BOOST_CHECK(!index.BlockUntilSyncedToCurrentChain());
    const EconomicsSnapshot before = index.Query(false);
    BOOST_CHECK(!before.amounts_authoritative);
    BOOST_CHECK(before.status == EconomicsStatus::Rebuilding || before.status == EconomicsStatus::Empty);
    BOOST_CHECK(!before.amounts.res_supply_available);
    BOOST_CHECK(!before.amounts.subsidy_available);

    BOOST_REQUIRE(index.Start());
    IndexWaitSynced(index);
    CBlockIndex* tip;
    {
        LOCK(cs_main);
        tip = m_node.chainman->ActiveChain().Tip();
    }
    const auto summary = index.ReadSummary(tip->GetBlockHash());
    BOOST_REQUIRE(summary);
    BOOST_CHECK_EQUAL(summary->height, tip->nHeight);
    BOOST_CHECK_EQUAL(summary->prefix_subsidy, Consensus::ComputeDerivedOruSubsidyCumulative(tip->nHeight, Params().GetConsensus()));
    BOOST_CHECK_EQUAL(summary->prefix_convergence, 0);
    BOOST_CHECK_EQUAL(summary->prefix_res_generated, 0);
    BOOST_CHECK(summary->res_supply_known);
    BOOST_CHECK_EQUAL(summary->prefix_res_supply, 0);
    BOOST_CHECK(summary->oru_supply_known);
    BOOST_CHECK(summary->fee_history_known);
    const EconomicsSnapshot synced = index.Query(true);
    BOOST_CHECK(synced.amounts_authoritative);
    BOOST_CHECK_EQUAL(synced.amounts.subsidy, summary->prefix_subsidy);
    BOOST_CHECK(synced.amounts.res_supply_available);
    BOOST_CHECK_EQUAL(synced.amounts.res_supply, 0);
    BOOST_CHECK(synced.amounts.res_generated_available);
    BOOST_CHECK(synced.status == EconomicsStatus::Ok);

    const uint256 discarded = tip->GetBlockHash();
    Chainstate& chainstate = m_node.chainman->ActiveChainstate();
    BlockValidationState state;
    BOOST_REQUIRE(chainstate.InvalidateBlock(state, tip));
    const CScript script = GetScriptForDestination(WitnessV0KeyHash(coinbaseKey.GetPubKey()));
    CreateAndProcessBlock({}, script);
    IndexWaitSynced(index);
    SyncWithValidationInterfaceQueue();
    BOOST_CHECK(!index.ReadSummary(discarded));
    const CBlockIndex* replacement;
    {
        LOCK(cs_main);
        replacement = m_node.chainman->ActiveChain().Tip();
    }
    BOOST_CHECK(replacement->GetBlockHash() != discarded);
    const auto replacement_summary = index.ReadSummary(replacement->GetBlockHash());
    BOOST_REQUIRE(replacement_summary);
    BOOST_CHECK_EQUAL(replacement_summary->prefix_subsidy, Consensus::ComputeDerivedOruSubsidyCumulative(replacement->nHeight, Params().GetConsensus()));
    index.Stop();
}

BOOST_AUTO_TEST_CASE(index_reloads_after_an_interrupted_commit)
{
    CBlockIndex* tip;
    {
        EconomicsIndex index{interfaces::MakeChain(m_node), 1 << 20, false};
        BOOST_REQUIRE(index.Start());
        IndexWaitSynced(index);
        LOCK(cs_main);
        tip = m_node.chainman->ActiveChain().Tip();
        index.Stop();
    }
    EconomicsIndex reloaded{interfaces::MakeChain(m_node), 1 << 20, false};
    BOOST_REQUIRE(reloaded.Start());
    IndexWaitSynced(reloaded);
    const auto summary = reloaded.ReadSummary(tip->GetBlockHash());
    BOOST_REQUIRE(summary);
    BOOST_CHECK_EQUAL(summary->prefix_subsidy, Consensus::ComputeDerivedOruSubsidyCumulative(tip->nHeight, Params().GetConsensus()));
    reloaded.Stop();
}

BOOST_AUTO_TEST_SUITE_END()
