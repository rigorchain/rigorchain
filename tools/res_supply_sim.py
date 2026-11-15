#!/usr/bin/env python3
"""Floating-point comparison of two epoch models. Not production issuance.

This script does not implement GetBlockSubsidy, CheckResMintArithmetic, or
transaction validation. It does not report chain supply.

Production subsidy is 0 ORU at height 0 and 128 ORU at every height >= 1.
Production RES_MAX_SUPPLY is 2^22 RES (4,194,304 RES, 419,430,400,000,000
atoms at 1e8 atoms per RES).

The numbers below are this experiment only: a 21,000,000 unit cap, a 1e6
display unit, epoch subsidies of 1000 then 125, and a lottery-versus-rate
comparison. Heights are epoch indexes 0..250 times 210,000 blocks. Totals
are floating-point products, not integer mint floors.
"""
import json
import math

RES_COIN = 1_000_000
COIN = 100_000_000
RES_REWARD_GENESIS = 40 * RES_COIN
RES_REWARD_EPOCH = 210_000
RES_REWARD_LAST_EPOCH = 250
# Experiment cap. Not RES_MAX_SUPPLY from res_issuance.h (2^22 RES).
EXPERIMENT_CAP = 21_000_000 * RES_COIN
C_ORU_PER_MINT = 1024          # 2^(256-246) / 1e8, in whole ORU
ORU_GENESIS_SUBSIDY = 1000
ORU_TAIL_SUBSIDY = 125


def isqrt(n):
    return math.isqrt(n)


def res_reward_for_epoch(k):
    """W(k) in atoms — bit-identical to ResRewardForEpoch."""
    if k < 0 or k > RES_REWARD_LAST_EPOCH:
        return 0
    e = k + 1
    scale = isqrt((e * e * e) << 32)
    return (RES_REWARD_GENESIS << 16) // scale


def oru_subsidy_for_epoch(k):
    """ORU per block in epoch k: 1000 halving 3x then flat 125."""
    if k >= 3:
        return ORU_TAIL_SUBSIDY
    return ORU_GENESIS_SUBSIDY >> k


def simulate(rho):
    """rho = fraction of freshly emitted ORU that gets burned for RES."""
    rows = []
    cum_lottery = 0
    cum_deterministic = 0
    cum_oru_emitted = 0
    cum_oru_burned = 0
    for k in range(RES_REWARD_LAST_EPOCH + 1):
        w = res_reward_for_epoch(k)          # atoms per mint
        sub = oru_subsidy_for_epoch(k)       # ORU per block
        burn_per_block = rho * sub           # ORU per block
        lam = burn_per_block / C_ORU_PER_MINT  # expected eligible candidates/block

        # Mechanism A: at most one mint per block, P(mint) = 1 - e^-lambda
        res_per_block_lottery = w * (1.0 - math.exp(-lam))
        # Mechanism B: RES = burn / RATE, capped at S(h) = W(k)
        res_per_block_det = w * min(lam, 1.0)

        ep_lottery = res_per_block_lottery * RES_REWARD_EPOCH
        ep_det = res_per_block_det * RES_REWARD_EPOCH

        cum_lottery += ep_lottery
        cum_deterministic += ep_det
        cum_oru_emitted += sub * RES_REWARD_EPOCH
        cum_oru_burned += burn_per_block * RES_REWARD_EPOCH

        rate_floor = C_ORU_PER_MINT / (w / RES_COIN) if w else float('inf')
        eff_cost_lottery = (rate_floor * lam / (1.0 - math.exp(-lam))) if lam > 0 else rate_floor
        eff_cost_det = rate_floor * max(lam, 1.0) if lam > 0 else rate_floor

        rows.append({
            "epoch": k,
            "w_res": w / RES_COIN,
            "oru_subsidy": sub,
            "lambda": lam,
            "epoch_res_lottery": ep_lottery / RES_COIN,
            "epoch_res_det": ep_det / RES_COIN,
            "cum_res_lottery": cum_lottery / RES_COIN,
            "cum_res_det": cum_deterministic / RES_COIN,
            "rate_floor_oru_per_res": rate_floor,
            "eff_cost_lottery": eff_cost_lottery,
            "eff_cost_det": eff_cost_det,
            "cum_oru_emitted": cum_oru_emitted,
            "cum_oru_burned": cum_oru_burned,
        })
    return rows


def main():
    scenarios = {}
    for rho in (1.0, 0.5, 0.2, 0.05, 0.01):
        rows = simulate(rho)
        scenarios[f"{rho:g}"] = rows
        tot_l = rows[-1]["cum_res_lottery"]
        tot_d = rows[-1]["cum_res_det"]
        e0_l = rows[0]["epoch_res_lottery"]
        e0_d = rows[0]["epoch_res_det"]
        print(f"rho={rho:>5.2f}  experiment lottery total={tot_l:12,.0f} units "
              f"({tot_l/21e6:6.2%} of the 21000000-unit experiment cap)"
              f"  epoch0 share={e0_l/tot_l:6.2%}"
              f"   |  experiment rate total={tot_d:12,.0f} ({tot_d/21e6:6.2%})"
              f"  epoch0 share={e0_d/tot_d:6.2%}"
              f"   |  rate/lottery={tot_d/tot_l:5.2f}x")

    print()
    print("schedule sanity:")
    sched_sum = sum(res_reward_for_epoch(k) for k in range(RES_REWARD_LAST_EPOCH + 1))
    print(f"  sum W(k)        = {sched_sum/RES_COIN:,.6f} experiment units")
    print(f"  x epoch length  = {sched_sum*RES_REWARD_EPOCH/RES_COIN:,.0f} experiment units (model maximum)")
    print(f"  experiment cap  = {EXPERIMENT_CAP/RES_COIN:,.0f} units "
          f"(not production 2^22 RES)")
    print(f"  W(0)={res_reward_for_epoch(0)/RES_COIN}  W(1)={res_reward_for_epoch(1)/RES_COIN:.6f}"
          f"  W(10)={res_reward_for_epoch(10)/RES_COIN:.6f}  W(250)={res_reward_for_epoch(250)/RES_COIN:.6f}")
    print(f"  RATE(0)={C_ORU_PER_MINT/(res_reward_for_epoch(0)/RES_COIN):.2f} ORU/RES"
          f"  RATE(250)={C_ORU_PER_MINT/(res_reward_for_epoch(250)/RES_COIN):,.0f} ORU/RES")

    with open("/tmp/res_sim.json", "w") as f:
        json.dump(scenarios, f)
    print("\nwrote /tmp/res_sim.json")


if __name__ == "__main__":
    main()
