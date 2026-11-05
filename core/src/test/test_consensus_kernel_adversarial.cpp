// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// P0-063: adversarial security baseline against the CLOSED Consensus Kernel /
// Transaction Kernel boundary. Does not redefine production formulas.

#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/params.h>
#include <consensus/rigor_consensus_lock.h>
#include <consensus/rigor_issuance.h>
#include <consensus/rigor_supply.h>
#include <consensus/validation.h>
#include <crypto/ed25519_provider.h>
#include <hash.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/predicate_verify.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/standard.h>
#include <test/util/setup_common.h>
#include <test/util/transaction_utils.h>
#include <uint256.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

const int kAttackHeights[] = {0, 1, 99, 100, 101, 209999, 210000, 210001, 419999, 420000};

CScript OruP2wpkh(unsigned char fill = 0x11)
{
    return CScript() << RIGOR_PUSH_0 << std::vector<unsigned char>(WITNESS_V0_KEYHASH_SIZE, fill);
}

CScript ResNative(unsigned char fill = 0xaa)
{
    return CScript() << RIGOR_PUSH_1 << std::vector<unsigned char>(WITNESS_V1_RES_SIZE, fill);
}

class SpendHeightChecker : public BaseSignatureChecker
{
    int m_height;
public:
    explicit SpendHeightChecker(int height) : m_height(height) {}
    int GetSpendHeight() const override { return m_height; }
};

CAmount ExpectedOruSubsidy(int height, const Consensus::Params& c)
{
    (void)c;
    if (height <= 0) return 0;
    return Consensus::ORU_FLAT_BLOCK_SUBSIDY;
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(test_consensus_kernel_adversarial, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(issuance_adversarial_boundaries)
{
    const auto& c = Params().GetConsensus();
    BOOST_REQUIRE_EQUAL(Consensus::RES_REWARD_EPOCH, 210000);

    for (int h : kAttackHeights) {
        const CAmount oru = GetBlockSubsidy(h, c);
        const RigorIssuance pack = ComputeRigorIssuance(h, c);
        BOOST_REQUIRE_MESSAGE(oru == ExpectedOruSubsidy(h, c), "CONSENSUS_ISSUANCE_ATTACK_FOUND");
        BOOST_REQUIRE_MESSAGE(pack.oru_subsidy == oru && pack.epoch == RigorIssuanceEpoch(h),
            "CONSENSUS_ISSUANCE_ATTACK_FOUND");
        BOOST_REQUIRE_MESSAGE(pack.epoch == h / Consensus::RES_REWARD_EPOCH, "CONSENSUS_ISSUANCE_ATTACK_FOUND");
    }

    BOOST_REQUIRE_MESSAGE(GetBlockSubsidy(0, c) == 0, "CONSENSUS_ISSUANCE_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(GetBlockSubsidy(0, c) == Consensus::ORU_GENESIS_BLOCK_SUBSIDY,
        "CONSENSUS_ISSUANCE_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(ComputeRigorIssuance(0, c).oru_subsidy == 0, "CONSENSUS_ISSUANCE_ATTACK_FOUND");

    RigorSupplyDeltaInput genesis;
    genesis.height = 0;
    BlockValidationState state;
    RigorSupplyDeltaResult out;
    BOOST_REQUIRE_MESSAGE(RigorSupplyDelta(genesis, c, out, state), "CONSENSUS_ISSUANCE_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(out.oru_subsidy == 0 && out.oru_circulating_delta == 0 && out.res_delta == 0,
        "CONSENSUS_ISSUANCE_ATTACK_FOUND");

    BOOST_REQUIRE_MESSAGE(GetBlockSubsidy(209999, c) == Consensus::ORU_FLAT_BLOCK_SUBSIDY,
        "CONSENSUS_ISSUANCE_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(GetBlockSubsidy(210000, c) == Consensus::ORU_FLAT_BLOCK_SUBSIDY,
        "CONSENSUS_ISSUANCE_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(GetBlockSubsidy(419999, c) == Consensus::ORU_FLAT_BLOCK_SUBSIDY,
        "CONSENSUS_ISSUANCE_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(GetBlockSubsidy(420000, c) == Consensus::ORU_FLAT_BLOCK_SUBSIDY,
        "CONSENSUS_ISSUANCE_ATTACK_FOUND");

    const CAmount max_h = GetBlockSubsidy(std::numeric_limits<int>::max(), c);
    BOOST_REQUIRE_MESSAGE(max_h == Consensus::ORU_FLAT_BLOCK_SUBSIDY,
        "CONSENSUS_ISSUANCE_ATTACK_FOUND");

    const int as_uint_max = static_cast<int>(std::numeric_limits<uint32_t>::max());
    BOOST_REQUIRE_MESSAGE(as_uint_max < 0, "CONSENSUS_ISSUANCE_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(GetBlockSubsidy(-1, c) == 0,
        "CONSENSUS_ISSUANCE_ATTACK_FOUND");

    BOOST_REQUIRE_MESSAGE(GetBlockSubsidy(630000, c) == Consensus::ORU_FLAT_BLOCK_SUBSIDY, "CONSENSUS_ISSUANCE_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(GetBlockSubsidy(840000, c) == Consensus::ORU_FLAT_BLOCK_SUBSIDY, "CONSENSUS_ISSUANCE_ATTACK_FOUND");
}

BOOST_AUTO_TEST_CASE(maturity_adversarial_early_spend)
{
    BOOST_REQUIRE_EQUAL(COINBASE_MATURITY, 100);
    BOOST_REQUIRE_EQUAL(RES_MINT_MATURITY, 100);
    BOOST_REQUIRE_EQUAL(Params().GetConsensus().nCoinbaseMaturity, 100);

    auto oru_mature = [](int source_height, int spend_height) {
        return spend_height - source_height >= COINBASE_MATURITY;
    };
    auto res_mature = [](int source_height, int spend_height) {
        return spend_height >= source_height + RES_MINT_MATURITY;
    };

    BOOST_REQUIRE_MESSAGE(!oru_mature(0, 99), "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(oru_mature(0, 100), "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(!oru_mature(1, 100), "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(oru_mature(1, 101), "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(!res_mature(0, 99), "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(res_mature(0, 100), "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(!res_mature(1, 100), "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(res_mature(1, 101), "MATURITY_BYPASS_FOUND");

    const int source = 1;
    const int lock = source + RES_MINT_MATURITY;
    const CScript mint = GetResMintScript(lock, uint160());
    const unsigned int flags = SCRIPT_VERIFY_SIGPUSHONLY | SCRIPT_VERIFY_CLEANSTACK;

    ScriptError early{};
    BOOST_REQUIRE_MESSAGE(!VerifyRigorPredicate(CScript(), mint, nullptr, flags, SpendHeightChecker(99), &early),
        "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(early == SCRIPT_ERR_UNSATISFIED_LOCKTIME, "MATURITY_BYPASS_FOUND");

    ScriptError at99{};
    BOOST_REQUIRE_MESSAGE(!VerifyRigorPredicate(CScript(), mint, nullptr, flags, SpendHeightChecker(100), &at99),
        "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(at99 == SCRIPT_ERR_UNSATISFIED_LOCKTIME, "MATURITY_BYPASS_FOUND");

    ScriptError exactly{};
    BOOST_REQUIRE_MESSAGE(!VerifyRigorPredicate(CScript(), mint, nullptr, flags, SpendHeightChecker(lock), &exactly),
        "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(exactly != SCRIPT_ERR_UNSATISFIED_LOCKTIME, "MATURITY_BYPASS_FOUND");

    ScriptError overflow{};
    const int64_t lock_past_int_max = 3000000000LL;
    BOOST_REQUIRE(lock_past_int_max > std::numeric_limits<int>::max());
    const CScript far_script = GetResMintScript(lock_past_int_max, uint160());
    BOOST_REQUIRE_MESSAGE(
        !VerifyRigorPredicate(CScript(), far_script, nullptr, flags, SpendHeightChecker(std::numeric_limits<int>::max()), &overflow),
        "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(overflow == SCRIPT_ERR_UNSATISFIED_LOCKTIME, "MATURITY_BYPASS_FOUND");
    ScriptError unencodable{};
    const CScript huge = GetResMintScript(std::numeric_limits<int64_t>::max(), uint160());
    BOOST_REQUIRE_MESSAGE(
        !VerifyRigorPredicate(CScript(), huge, nullptr, flags, SpendHeightChecker(std::numeric_limits<int>::max()), &unencodable),
        "MATURITY_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(
        unencodable == SCRIPT_ERR_UNSATISFIED_LOCKTIME || unencodable == SCRIPT_ERR_RIGOR_SCRIPT_TYPE,
        "MATURITY_BYPASS_FOUND");
}

BOOST_AUTO_TEST_CASE(classification_adversarial)
{
    const CScript oru = OruP2wpkh();
    const CScript res = ResNative();
    const CScript mint = GetResMintScript(100, uint160());
    const CScript converge = GetConvergeScript(uint160());
    const CScript empty;
    const CScript truncated = CScript() << RIGOR_PUSH_0 << std::vector<unsigned char>(19, 0x11);
    const CScript unknown_ver = CScript() << RIGOR_PUSH_2 << std::vector<unsigned char>(WITNESS_V0_KEYHASH_SIZE, 0x11);
    const std::vector<unsigned char> huge(10 * 1024, 0xff);
    const CScript oversized(huge.begin(), huge.end());
    const CScript malformed = CScript() << CScriptNum(0) << std::vector<unsigned char>(19, 0x11) << RIGOR_MARKER_RES_MINT;
    const CScript random_bytes = CScript() << std::vector<unsigned char>{0xde, 0xad, 0xbe, 0xef, 0x00};

    auto must_match = [](const CScript& s, AssetType expect) {
        std::vector<std::vector<unsigned char>> sols;
        const TxoutType t = ClassifyRigorTxout(s, sols);
        const AssetType a = ClassifyRigorAsset(s);
        BOOST_REQUIRE_MESSAGE(a == expect, "CLASSIFICATION_BYPASS_FOUND");
        BOOST_REQUIRE_MESSAGE(a == AssetTypeFromScriptPubKey(s), "CLASSIFICATION_BYPASS_FOUND");
        BOOST_REQUIRE_MESSAGE(Solver(s, sols) == t, "CLASSIFICATION_BYPASS_FOUND");
        (void)t;
    };

    must_match(oru, ASSET_ORU);
    must_match(res, ASSET_RES);
    must_match(mint, ASSET_RES);
    must_match(converge, ASSET_ORU);
    must_match(empty, ASSET_INVALID);
    must_match(truncated, ASSET_INVALID);
    must_match(unknown_ver, ASSET_INVALID);
    must_match(oversized, ASSET_INVALID);
    must_match(malformed, ASSET_INVALID);
    must_match(random_bytes, ASSET_INVALID);

    std::vector<std::vector<unsigned char>> sols;
    BOOST_REQUIRE_MESSAGE(ClassifyRigorTxout(oru, sols) == TxoutType::WITNESS_V0_KEYHASH,
        "CLASSIFICATION_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(ClassifyRigorTxout(res, sols) == TxoutType::WITNESS_V1_RES,
        "CLASSIFICATION_BYPASS_FOUND");
}

BOOST_AUTO_TEST_CASE(supply_delta_adversarial)
{
    const auto& c = Params().GetConsensus();
    const int h = 1;
    const CAmount subsidy = GetBlockSubsidy(h, c);
    const CAmount body_mint = 40 * Consensus::RES_COIN;

    auto run = [&](const RigorSupplyDeltaInput& in, bool expect_ok, const char* reject = nullptr) {
        BlockValidationState poisoned;
        poisoned.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "poison-must-be-sink-only");
        RigorSupplyDeltaResult out;
        const bool ok = RigorSupplyDelta(in, c, out, poisoned);
        if (expect_ok) {
            BOOST_REQUIRE_MESSAGE(ok, "SUPPLY_INFLATION_ATTACK_FOUND");
            BOOST_REQUIRE_MESSAGE(out.oru_subsidy == subsidy, "SUPPLY_INFLATION_ATTACK_FOUND");
            BOOST_REQUIRE_MESSAGE(out.oru_circulating_delta == subsidy - in.converge_sum,
                "SUPPLY_INFLATION_ATTACK_FOUND");
            BOOST_REQUIRE_MESSAGE(out.res_delta == in.res_mint, "SUPPLY_INFLATION_ATTACK_FOUND");
            BOOST_REQUIRE_MESSAGE(out.oru_claimable == (in.fees_oru - in.fee_burn) + subsidy,
                "SUPPLY_INFLATION_ATTACK_FOUND");
        } else {
            BOOST_REQUIRE_MESSAGE(!ok, "SUPPLY_INFLATION_ATTACK_FOUND");
            BOOST_REQUIRE(reject != nullptr);
            BOOST_REQUIRE_MESSAGE(poisoned.GetRejectReason() == reject, "SUPPLY_INFLATION_ATTACK_FOUND");
        }
        return out;
    };

    RigorSupplyDeltaInput none;
    none.height = h;
    none.oru_coinbase = subsidy;
    run(none, true);

    RigorSupplyDeltaInput mint;
    mint.height = h;
    mint.res_mint = body_mint;
    mint.oru_coinbase = subsidy;
    const RigorSupplyDeltaResult once = run(mint, true);
    const RigorSupplyDeltaResult twice = run(mint, true);
    BOOST_REQUIRE_MESSAGE(once.res_delta == body_mint && twice.res_delta == body_mint,
        "SUPPLY_INFLATION_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(once.res_delta != 2 * body_mint, "SUPPLY_INFLATION_ATTACK_FOUND");

    RigorSupplyDeltaInput burn = none;
    burn.converge_sum = subsidy + COIN;
    const RigorSupplyDeltaResult burned = run(burn, true);
    BOOST_REQUIRE_MESSAGE(burned.oru_circulating_delta == -COIN, "SUPPLY_INFLATION_ATTACK_FOUND");
    BOOST_REQUIRE_MESSAGE(burned.oru_circulating_delta < 0, "SUPPLY_INFLATION_ATTACK_FOUND");

    RigorSupplyDeltaInput overflow = none;
    overflow.oru_coinbase = std::numeric_limits<CAmount>::max();
    run(overflow, false, "bad-cb-amount");

    RigorSupplyDeltaInput fee = none;
    fee.fees_oru = 100 * COIN;
    fee.fee_burn = 10 * COIN;
    fee.oru_coinbase = subsidy + 100 * COIN;
    run(fee, false, "bad-cb-amount");
    fee.oru_coinbase = subsidy + 90 * COIN;
    run(fee, true);

    RigorSupplyDeltaInput burn_edge = none;
    burn_edge.fees_oru = 50 * COIN;
    burn_edge.fee_burn = 50 * COIN;
    burn_edge.oru_coinbase = subsidy;
    run(burn_edge, true);
    burn_edge.fee_burn = 51 * COIN;
    burn_edge.oru_coinbase = subsidy;
    run(burn_edge, false, "bad-cb-amount");
}

BOOST_AUTO_TEST_CASE(transaction_kernel_adversarial_fail_closed)
{
    const std::vector<unsigned char> seed = ParseHex("00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff");
    std::vector<unsigned char> pubkey(ed25519::PUBKEY_SIZE);
    std::vector<unsigned char> seckey(ed25519::SECRETKEY_SIZE);
    BOOST_REQUIRE(ed25519::SeedToKeypair(seed, pubkey, seckey));

    const uint160 keyhash = Hash160(pubkey);
    const CScript scriptPubKey = GetScriptForDestination(WitnessV0KeyHash(keyhash));
    const CTransaction txCredit = CTransaction(BuildCreditingTransaction(scriptPubKey, COIN));
    CMutableTransaction txSpend = BuildSpendingTransaction(CScript(), CScriptWitness(), txCredit);
    PrecomputedTransactionData txdata(txSpend);
    const uint256 sighash = SignatureHash(scriptPubKey, txSpend, 0, txCredit.vout[0].nValue, SigVersion::WITNESS_V0,
        TestSighashChainId(), &txdata);
    std::vector<unsigned char> witSig(ed25519::SIGNATURE_SIZE);
    BOOST_REQUIRE(ed25519::SignDetached(Span{sighash.begin(), 32}, seckey, Span{witSig.data(), ed25519::SIGNATURE_SIZE}));

    auto verify = [&](const CScript& sig, const CScript& spk, const CScriptWitness* wit, const CMutableTransaction& tx,
                      const uint256& chain) {
        ScriptError err{SCRIPT_ERR_UNKNOWN_ERROR};
        PrecomputedTransactionData data(tx);
        const bool ok = VerifyRigorPredicate(sig, spk, wit, STANDARD_SCRIPT_VERIFY_FLAGS,
            MutableTransactionSignatureChecker(&tx, 0, COIN, data, MissingDataBehavior::ASSERT_FAIL, chain), &err);
        return std::pair<bool, ScriptError>{ok, err};
    };

    CScriptWitness good;
    good.stack.push_back(witSig);
    good.stack.push_back(pubkey);
    txSpend.vin[0].scriptWitness = good;
    const auto ok = verify(CScript(), scriptPubKey, &txSpend.vin[0].scriptWitness, txSpend, TestSighashChainId());
    BOOST_REQUIRE_MESSAGE(ok.first, "TRANSACTION_KERNEL_BYPASS_FOUND");

    CScriptWitness empty_sig;
    empty_sig.stack.push_back(std::vector<unsigned char>{});
    empty_sig.stack.push_back(pubkey);
    txSpend.vin[0].scriptWitness = empty_sig;
    BOOST_REQUIRE_MESSAGE(!verify(CScript(), scriptPubKey, &txSpend.vin[0].scriptWitness, txSpend, TestSighashChainId()).first,
        "TRANSACTION_KERNEL_BYPASS_FOUND");

    CScriptWitness malformed;
    malformed.stack.push_back(std::vector<unsigned char>(16, 0x30));
    malformed.stack.push_back(pubkey);
    txSpend.vin[0].scriptWitness = malformed;
    BOOST_REQUIRE_MESSAGE(!verify(CScript(), scriptPubKey, &txSpend.vin[0].scriptWitness, txSpend, TestSighashChainId()).first,
        "TRANSACTION_KERNEL_BYPASS_FOUND");

    std::vector<unsigned char> wrong_pk = pubkey;
    wrong_pk[0] ^= 0x01;
    CScriptWitness wrong;
    wrong.stack.push_back(witSig);
    wrong.stack.push_back(wrong_pk);
    txSpend.vin[0].scriptWitness = wrong;
    BOOST_REQUIRE_MESSAGE(!verify(CScript(), scriptPubKey, &txSpend.vin[0].scriptWitness, txSpend, TestSighashChainId()).first,
        "TRANSACTION_KERNEL_BYPASS_FOUND");

    CScriptWitness flipped = good;
    flipped.stack[0][0] ^= 0x01;
    txSpend.vin[0].scriptWitness = flipped;
    BOOST_REQUIRE_MESSAGE(!verify(CScript(), scriptPubKey, &txSpend.vin[0].scriptWitness, txSpend, TestSighashChainId()).first,
        "TRANSACTION_KERNEL_BYPASS_FOUND");

    txSpend.vin[0].scriptWitness = good;
    const uint256 other_chain = uint256S("0xdeadbeef");
    BOOST_REQUIRE_MESSAGE(!verify(CScript(), scriptPubKey, &txSpend.vin[0].scriptWitness, txSpend, other_chain).first,
        "TRANSACTION_KERNEL_BYPASS_FOUND");

    CMutableTransaction mutated = txSpend;
    mutated.vout[0].nValue = COIN - 1;
    mutated.vin[0].scriptWitness = good;
    BOOST_REQUIRE_MESSAGE(!verify(CScript(), scriptPubKey, &mutated.vin[0].scriptWitness, mutated, TestSighashChainId()).first,
        "TRANSACTION_KERNEL_BYPASS_FOUND");

    const CScript res = ResNative();
    txSpend.vin[0].scriptWitness = good;
    BOOST_REQUIRE_MESSAGE(!verify(CScript(), res, &txSpend.vin[0].scriptWitness, txSpend, TestSighashChainId()).first,
        "TRANSACTION_KERNEL_BYPASS_FOUND");

    ScriptError unknown{};
    BOOST_REQUIRE_MESSAGE(!VerifyRigorPredicate(CScript(), CScript(), nullptr, STANDARD_SCRIPT_VERIFY_FLAGS,
                                BaseSignatureChecker(), &unknown),
        "TRANSACTION_KERNEL_BYPASS_FOUND");
    BOOST_REQUIRE_MESSAGE(unknown == SCRIPT_ERR_RIGOR_SCRIPT_TYPE, "TRANSACTION_KERNEL_BYPASS_FOUND");
}

BOOST_AUTO_TEST_SUITE_END()
