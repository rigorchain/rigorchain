#!/usr/bin/env python3
"""Epoch unit-price experiment. Not the production issuance schedule.

`GetBlockSubsidy` pays 0 ORU at height 0 and 128 ORU at every height >= 1.
This file does not compute that function. Its epoch table, 1 000 ORU first
epoch and 125 ORU tail are parameters of this experiment only. Its derived
sum is not `RES_MAX_SUPPLY` (`2^22` RES) and is not chain issuance.

Nothing here uses floating point for the price curve. The arithmetic is the
integer epoch model, kept so that model can still be reconstructed.

    python3 tools/res_issuance_schedule.py             human-readable report
    python3 tools/res_issuance_schedule.py --json      machine-readable
    python3 tools/res_issuance_schedule.py --markdown  RFC tables
"""
import argparse
import json
import sys

# --- experiment parameters, not GetBlockSubsidy -----------------------------
COIN = 100_000_000               # display unit used by this experiment
RES_REWARD_EPOCH = 210_000       # blocks per epoch in this experiment
RES_REWARD_LAST_EPOCH = 250      # last epoch this experiment sums
ORU_GENESIS_SUBSIDY = 1_000      # experiment epoch-0 ORU, not height 0
ORU_TAIL_SUBSIDY = 125           # experiment tail, not the live 128 ORU subsidy

# --- decided by RFC-019 -----------------------------------------------------
RES_COIN = 100_000_000           # RES atoms per RES; matches ORU
RES_DESTROY_COST_GENESIS = 1024 * COIN   # ORU atoms to mint one whole RES at k=0
RES_DESTROY_POLE_OFFSET = 8              # k0 in the (k+k0)/k0 hardening curve

UINT64_MAX = (1 << 64) - 1
INT64_MAX = (1 << 63) - 1


def isqrt(n):
    """Floor square root. Mirrors ResIntegerSqrt in rigor_consensus_lock.h."""
    return __import__("math").isqrt(n)


def unit_destroy_cost(k):
    """Same integer as unit_price.

    The report called this name, and the module never defined it. The
    price function's own note is that epoch 0 equals RES_DESTROY_COST_GENESIS,
    so the report's genesis comparison is unit_price(0).
    """
    return unit_price(k)


def unit_price(k):
    """ORU atoms required to mint one whole RES during epoch k.

    RATE(k) = RATE(0) * ((k + k0) / k0)^(3/2), evaluated as

        RATE(k) = RATE(0) * isqrt(((k+k0)^3 << 32) / k0^3) >> 16

    The division by k0^3 sits *inside* the isqrt argument rather than outside.
    That placement is what keeps the product in range: hoisting it out makes
    the multiply reach 2.8e19 and overflow uint64 near k = 250.

    At k = 0 the isqrt argument is exactly 2^32, so the factor is exactly
    2^16 and RATE(0) == RES_DESTROY_COST_GENESIS with no rounding at all.
    """
    if k < 0 or k > RES_REWARD_LAST_EPOCH:
        return 0
    e = k + RES_DESTROY_POLE_OFFSET
    k0c = RES_DESTROY_POLE_OFFSET ** 3
    scale = isqrt(((e * e * e) << 32) // k0c)
    return (RES_DESTROY_COST_GENESIS * scale) >> 16


def oru_subsidy(k):
    """ORU per block during epoch k: 1000, halving three times, then flat."""
    return ORU_TAIL_SUBSIDY if k >= 3 else (ORU_GENESIS_SUBSIDY >> k)


def oru_emitted(k):
    """ORU atoms this experiment assigns to epoch k.

    Epoch 0 of the experiment counts 209 999 blocks, not 210 000, and then
    multiplies by this file's 1 000 ORU epoch amount. Production
    GetBlockSubsidy is 0 at height 0 and 128 ORU at every height >= 1.
    This function does not compute that subsidy.
    """
    blocks = RES_REWARD_EPOCH - 1 if k == 0 else RES_REWARD_EPOCH
    return blocks * oru_subsidy(k) * COIN


def max_res_from_epoch(k):
    """RES atoms mintable from one epoch's own ORU emission.

    res_atoms = floor(oru_atoms * RES_COIN / RATE(k)); the mint rule is the
    cross-multiplied form of the same inequality.
    """
    p = unit_price(k)
    return (oru_emitted(k) * RES_COIN) // p if p else 0


def min_atom_burn(k):
    """Smallest ORU burn that mints one RES atom: ceil(RATE(k) / RES_COIN).

    Wallet/UI helper. Consensus never divides; the R = 1 case of the mint
    inequality is this value.
    """
    p = unit_price(k)
    return (p + RES_COIN - 1) // RES_COIN if p else 0


def derived_cap():
    """Upper bound on RES that can ever exist.

    Total RES = sum over heights of burn(h)/RATE(h), subject to the prefix
    constraint that ORU cannot be burned before it is emitted. RATE is
    strictly increasing, so 1/RATE is strictly decreasing, and by the standard
    exchange argument the sum is maximised by burning every ORU in the epoch
    it appears. That maximum is the per-epoch sum below.

    It is an upper bound and not an estimate: fees, lost keys and ORU held
    rather than burned can only lower the sum this experiment attributes
    to that schedule. The sum is not production supply.
    """
    return sum(max_res_from_epoch(k) for k in range(RES_REWARD_LAST_EPOCH + 1))


def overflow_report(cap):
    """Worst-case intermediate values, against the 64-bit ceilings."""
    k = RES_REWARD_LAST_EPOCH
    e = k + RES_DESTROY_POLE_OFFSET
    k0c = RES_DESTROY_POLE_OFFSET ** 3
    isqrt_arg = ((e * e * e) << 32) // k0c
    scale = isqrt(isqrt_arg)
    price_mul = RES_DESTROY_COST_GENESIS * scale
    # The consensus check is res_out * RATE(k) <= burn * RES_COIN. Both sides
    # exceed 64 bits at cap scale, so the comparison needs a 128-bit
    # intermediate. It replaces the uint512 cross-multiply the draw needed.
    lhs = cap * unit_price(k)
    rhs = cap * unit_price(k)
    return [
        ("isqrt argument at k=250", isqrt_arg, UINT64_MAX, "uint64"),
        ("RATE(0) * scale at k=250", price_mul, UINT64_MAX, "uint64"),
        ("RATE(250)", unit_price(k), INT64_MAX, "int64 (CAmount)"),
        ("derived cap in atoms", cap, INT64_MAX, "int64 (CAmount)"),
        ("res_out * RATE at cap scale", max(lhs, rhs), UINT64_MAX,
         "uint64 -- EXCEEDS, needs 128-bit"),
    ]


def rows():
    out = []
    for k in range(RES_REWARD_LAST_EPOCH + 1):
        out.append({
            "epoch": k,
            "first_height": k * RES_REWARD_EPOCH,
            "destroy_cost_atoms": unit_price(k),
            "destroy_cost_oru": unit_price(k) / COIN,
            "min_atom_burn": min_atom_burn(k),
            "oru_subsidy": oru_subsidy(k),
            "max_res_atoms": max_res_from_epoch(k),
        })
    return out


def report():
    cap = derived_cap()
    r = rows()
    p0, pl = unit_destroy_cost(0), unit_price(RES_REWARD_LAST_EPOCH)

    print("Epoch unit-price experiment — not GetBlockSubsidy")
    print("Production subsidy: 0 ORU at height 0, 128 ORU at every height >= 1.")
    print("The ORU/blk column below is this experiment's epoch table, not that subsidy.")
    print("=" * 76)
    print(f"  RES_COIN               {RES_COIN:>24,}")
    print(f"  RES_DESTROY_COST_GENESIS {RES_DESTROY_COST_GENESIS:>24,} atoms "
          f"= {RES_DESTROY_COST_GENESIS//COIN:,} ORU")
    print(f"  RES_DESTROY_POLE_OFFSET  {RES_DESTROY_POLE_OFFSET:>24}")
    print(f"  RES_REWARD_EPOCH       {RES_REWARD_EPOCH:>24,}")
    print(f"  RES_REWARD_LAST_EPOCH  {RES_REWARD_LAST_EPOCH:>24}")
    print()
    exact = unit_destroy_cost(0) == RES_DESTROY_COST_GENESIS
    print(f"  RATE(0) == RES_DESTROY_COST_GENESIS exactly: {exact}")
    mono = all(unit_price(k) < unit_price(k + 1)
               for k in range(RES_REWARD_LAST_EPOCH))
    print(f"  RATE strictly increasing over 0..250:      {mono}")
    print(f"  hardening RATE(250)/RATE(0):               {pl/p0:.1f}x")
    print()
    print(f"  experiment sum {cap:>24,} atoms = {cap/RES_COIN:,.8f} units")
    print()

    print(f"{'epoch':>6} {'first height':>13} {'ORU/blk':>8} "
          f"{'RATE (ORU/RES)':>16} {'max RES this epoch':>20}")
    print("-" * 76)
    for k in (0, 1, 2, 3, 4, 5, 10, 20, 50, 100, 175, 250):
        d = r[k]
        print(f"{d['epoch']:>6} {d['first_height']:>13,} {d['oru_subsidy']:>8} "
              f"{d['destroy_cost_oru']:>16,.2f} "
              f"{d['max_res_atoms']/RES_COIN:>20,.2f}")
    print()

    print("overflow margins")
    print("-" * 76)
    for name, value, ceiling, where in overflow_report(cap):
        ratio = ceiling / value if value else float("inf")
        flag = "OK" if value <= ceiling else "OVERFLOW"
        print(f"  {name:<32} {value:>26,}  {flag:>8}  "
              f"{'headroom %.0fx' % ratio if flag == 'OK' else where}")
    print()
    print("  The last row is why the mint check needs a 128-bit intermediate.")
    print("  It replaces the uint512 cross-multiply the draw inequality used,")
    print("  so the arithmetic machinery gets strictly smaller, not larger.")
    print()

    print("experiment sum scaled by rho — not chain issuance")
    print("-" * 76)
    for rho_num, rho_den in ((5, 100), (20, 100), (50, 100), (100, 100)):
        scaled = sum(max_res_from_epoch(k) * rho_num // rho_den
                     for k in range(RES_REWARD_LAST_EPOCH + 1))
        print(f"  rho = {rho_num:>3}%   experiment sum {scaled/RES_COIN:>14,.0f} RES"
              f"   share of this experiment's sum {scaled/cap:>7.1%}")


def markdown():
    cap = derived_cap()
    print("| Name | Value |")
    print("|------|-------|")
    print(f"| `RES_COIN` | {RES_COIN:,} |".replace(",", " "))
    print(f"| `RES_DESTROY_COST_GENESIS` | {RES_DESTROY_COST_GENESIS:,} atoms "
          f"= {RES_DESTROY_COST_GENESIS//COIN:,} ORU |".replace(",", " "))
    print(f"| `RES_DESTROY_POLE_OFFSET` | {RES_DESTROY_POLE_OFFSET} |")
    print(f"| `RES_REWARD_EPOCH` | {RES_REWARD_EPOCH:,} blocks |".replace(",", " "))
    print(f"| `RES_REWARD_LAST_EPOCH` | {RES_REWARD_LAST_EPOCH} |")
    print(f"| experiment sum | {cap:,} atoms (this file, not production RES_MAX_SUPPLY) |".replace(",", " "))
    print()
    print("| Epoch | First height | ORU/block | RATE (ORU per RES) | Max RES from this epoch |")
    print("|------:|-------------:|----------:|-------------------:|------------------------:|")
    for k in (0, 1, 2, 3, 5, 10, 20, 50, 100, 250):
        print(f"| {k} | {k*RES_REWARD_EPOCH:,} | {oru_subsidy(k)} | "
              f"{unit_price(k)/COIN:,.2f} | {max_res_from_epoch(k)/RES_COIN:,.2f} |"
              .replace(",", " "))


def check_doc():
    """Report that the RFC-019 numeric agreement check no longer has a target.

    The flag was added to confirm that
    docs/archive/rfc/RFC-019-RES-DETERMINISTIC-UNIT-PRICE.md contained the
    integers this epoch experiment derives. That file is not in this public
    tree. RFC-020 describes GetBlockSubsidy and the destroy-cost predicate,
    not this epoch table, so it is not a substitute document. Returning
    success here would claim a check that was not performed.
    """
    print(
        "NOT_APPLICABLE: --check-doc compared this epoch experiment with "
        "docs/archive/rfc/RFC-019-RES-DETERMINISTIC-UNIT-PRICE.md. "
        "That file is not in this tree. RFC-020 is a different schedule and "
        "is not checked here. This command does not validate production issuance.",
        file=sys.stderr,
    )
    return 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--markdown", action="store_true")
    ap.add_argument("--check-doc", action="store_true",
                    help="report that the removed RFC-019 numeric check does not apply")
    a = ap.parse_args()
    if a.check_doc:
        sys.exit(check_doc())
    if a.json:
        json.dump({
            "constants": {
                "RES_COIN": RES_COIN,
                "RES_DESTROY_COST_GENESIS": RES_DESTROY_COST_GENESIS,
                "RES_DESTROY_POLE_OFFSET": RES_DESTROY_POLE_OFFSET,
                "RES_REWARD_EPOCH": RES_REWARD_EPOCH,
                "RES_REWARD_LAST_EPOCH": RES_REWARD_LAST_EPOCH,
                "experiment_sum_atoms": derived_cap(),
            },
            "schedule": rows(),
        }, sys.stdout, indent=2)
        print()
    elif a.markdown:
        markdown()
    else:
        report()


if __name__ == "__main__":
    main()
