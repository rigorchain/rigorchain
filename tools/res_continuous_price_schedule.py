#!/usr/bin/env python3
"""Integer model of the continuous RES destroy-cost inequality (RFC-020).

This file does not decide consensus and it does not report chain issuance.
`cumulative_res_at` is `floor(RES_MAX_SUPPLY * h / (h + 2^25))`, the integer
antiderivative, not the sum of per-block floors and not a balance the node
has validated. `rho * RES_MAX_SUPPLY` is that constant scaled by a fraction.
It is not realised issuance and not an on-chain total.

The predicate check uses Python ints for the same multiply-and-shift the C++
header uses: `R * (2^25 + h)^2 <= B << 40`.

The whole of RFC-020 reduces to one predicate,

    R * (2^25 + h)^2 <= B << 40

with no division, no floor and no square root anywhere in it. This script
derives that predicate from the destroy cost it encodes and checks that the
algebra is an identity. The printed tables are this model. They are not a
dump of validated chain supply.

    python3 tools/res_continuous_price_schedule.py             human-readable report
    python3 tools/res_continuous_price_schedule.py --json      machine-readable
    python3 tools/res_continuous_price_schedule.py --markdown  RFC tables
"""
import argparse
import json
import sys

# --- fixed by the existing protocol, not decided here -----------------------
COIN = 100_000_000               # ORU atoms per ORU
RES_COIN = 100_000_000           # RES atoms per RES
TARGET_BLOCK_TIME = 600          # seconds
MAX_MONEY = 10_000_000_000 * COIN
INT32_MAX = (1 << 31) - 1
UINT128_MAX = (1 << 128) - 1

# --- decided by RFC-020 -----------------------------------------------------
ORU_FLAT_BLOCK_SUBSIDY = 128 * COIN    # ORU atoms per block, every height >= 1
ORU_GENESIS_BLOCK_SUBSIDY = 0          # unchanged from P0-038
RES_DESTROY_COST_GENESIS = 1024 * COIN   # ORU atoms to mint one whole RES at h = 0
RES_DESTROY_POLE = 1 << 25               # blocks; the hardening timescale

# Derived shift, proved exact in `predicate_shift_is_exact` below.
RES_DESTROY_SHIFT = 40

BLOCKS_PER_YEAR = 365.2425 * 24 * 3600 / TARGET_BLOCK_TIME


# --------------------------------------------------------------------------
# 1. The destroy cost, and the predicate it collapses to
# --------------------------------------------------------------------------
def destroy_cost_atoms(height):
    """ORU atoms required to mint one whole RES at `height`.

    destroy_cost(h) = RES_DESTROY_COST_GENESIS * (1 + h / 2^25)^2
             = RES_DESTROY_COST_GENESIS * (2^25 + h)^2 / 2^50

    Floor-divided, and reported for display only. Consensus never evaluates
    this function: it evaluates `check_mint` below, which is the same relation
    with the division cleared. Keeping the division out of consensus is what
    removes every rounding-direction question from the rule.
    """
    if height < 0:
        return 0
    return (RES_DESTROY_COST_GENESIS * (RES_DESTROY_POLE + height) ** 2) >> 50


def check_mint(res_atoms, burn_atoms, height):
    """The entire RFC-020 consensus rule.

    Mint `res_atoms` RES by destroying `burn_atoms` ORU in a transaction that
    confirms at `height`. Returns True when the burn covers the mint.

        R * (2^25 + h)^2 <= B << 40

    Derivation, an identity in exact arithmetic:

        R * destroy_cost(h)                      <= B * RES_COIN
        R * 1024e8 * (2^25+h)^2 / 2^50    <= B * 1e8
        R * 1024   * (2^25+h)^2           <= B * 2^50
        R * 2^10   * (2^25+h)^2           <= B * 2^50
        R          * (2^25+h)^2           <= B * 2^40
    """
    if res_atoms < 0 or burn_atoms < 0 or height < 0:
        return False
    if res_atoms == 0:
        return True          # a plain burn stays legal, any positive B
    if res_atoms > res_max_supply_atoms():
        return False
    if burn_atoms > MAX_MONEY:
        return False
    return res_atoms * (RES_DESTROY_POLE + height) ** 2 <= (burn_atoms << RES_DESTROY_SHIFT)


def min_burn_for_one_atom(height):
    """Smallest ORU burn that mints one RES atom at `height`. A theorem of
    check_mint, not a second consensus constant."""
    return -(-((RES_DESTROY_POLE + height) ** 2) // (1 << RES_DESTROY_SHIFT))


def max_mint_for_burn(burn_atoms, height):
    """Largest RES the wallet may claim for a given burn. Wallet-side only."""
    if burn_atoms <= 0 or height < 0:
        return 0
    return (burn_atoms << RES_DESTROY_SHIFT) // ((RES_DESTROY_POLE + height) ** 2)


def block_subsidy_atoms(height):
    """ORU atoms issued by the coinbase at `height`. Flat forever, no halving."""
    if height < 0:
        return 0
    if height == 0:
        return ORU_GENESIS_BLOCK_SUBSIDY
    return ORU_FLAT_BLOCK_SUBSIDY


# --------------------------------------------------------------------------
# 2. Supply
# --------------------------------------------------------------------------
def res_max_supply_atoms():
    """The cap, in closed form.

        cap = (S / P0) * H = 2^7 / 2^10 * 2^25 = 2^(7-10+25) = 2^22 RES

    Integral of the per-block mint ceiling S/destroy_cost(h) from 0 to infinity.
    Because the exponent is exactly 2, that integral is exactly (S/P0)*H with
    no residual term, which is why the cap lands on a power of two rather than
    near one.
    """
    return (1 << 22) * RES_COIN


def cumulative_res_at(height):
    """Max RES mintable through `height` under rho = 100%.

        cap * h / (h + H)

    Exact antiderivative of the per-block ceiling; no summation needed.
    """
    cap = res_max_supply_atoms()
    return (cap * height) // (height + RES_DESTROY_POLE)


def cumulative_oru_at(height):
    """Cumulative ORU issued through `height`, ignoring burns."""
    if height <= 0:
        return 0
    return height * ORU_FLAT_BLOCK_SUBSIDY


def natural_terminus():
    """Height at which a whole block's subsidy no longer buys one RES atom.

    Minting closes here by integer arithmetic alone. RFC-020 carries no
    horizon constant; this height is a consequence, not a parameter.
    """
    lo, hi = 0, 1
    while max_mint_for_burn(ORU_FLAT_BLOCK_SUBSIDY, hi) > 0:
        hi *= 2
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if max_mint_for_burn(ORU_FLAT_BLOCK_SUBSIDY, mid) > 0:
            lo = mid
        else:
            hi = mid
    return lo


# --------------------------------------------------------------------------
# 3. Self-checks. Every claim RFC-020 makes is asserted here.
# --------------------------------------------------------------------------
def predicate_shift_is_exact():
    """RES_DESTROY_SHIFT is forced, not chosen: 50 - log2(P0/COIN) = 50 - 10."""
    p0_oru = RES_DESTROY_COST_GENESIS // COIN
    assert p0_oru == 1024 and p0_oru.bit_length() - 1 == 10
    assert RES_DESTROY_SHIFT == 50 - 10
    return True


def predicate_matches_destroy_cost():
    """check_mint agrees with the floor-divided destroy cost at every boundary.

    For each height, the smallest burn that satisfies check_mint for one whole
    RES must equal the displayed destroy cost, and one atom less must fail.
    """
    for height in (0, 1, 1000, 52_594, 5_259_492, RES_DESTROY_POLE,
                   52_594_920, 1 << 30, INT32_MAX):
        need = -(-(RES_COIN * (RES_DESTROY_POLE + height) ** 2) // (1 << RES_DESTROY_SHIFT))
        assert check_mint(RES_COIN, need, height)
        assert not check_mint(RES_COIN, need - 1, height)
        assert abs(need - destroy_cost_atoms(height)) <= 1
    return True


def genesis_destroy_cost_is_exact():
    """At h = 0 the destroy cost is 1024 ORU with no rounding whatsoever, and the
    minimum burn for one RES atom is 1024 ORU atoms -- both bit-identical to
    the RFC-019 values they replace."""
    assert destroy_cost_atoms(0) == RES_DESTROY_COST_GENESIS == 1024 * COIN
    assert min_burn_for_one_atom(0) == 1024
    return True


def destroy_cost_strictly_increasing():
    """Required by the cap argument: delaying a burn must never buy more RES."""
    probes = [0, 1, 2, 1000, 52_594, RES_DESTROY_POLE - 1, RES_DESTROY_POLE,
              RES_DESTROY_POLE + 1, 1 << 30, INT32_MAX - 1]
    for height in probes:
        assert destroy_cost_atoms(height + 1) >= destroy_cost_atoms(height)
        assert max_mint_for_burn(ORU_FLAT_BLOCK_SUBSIDY, height + 1) \
            <= max_mint_for_burn(ORU_FLAT_BLOCK_SUBSIDY, height)
    # strict over any interval long enough for the floor to advance
    assert destroy_cost_atoms(1 << 20) > destroy_cost_atoms(0)
    return True


def no_free_mint_anywhere():
    """There is no height at which a zero or negative burn mints RES, and no
    height at which the destroy cost degenerates to zero. RFC-019 needed a separate
    horizon predicate precisely because its destroy cost could return 0; the
    continuous destroy cost never can, so the failure mode does not exist here."""
    for height in (0, 1, RES_DESTROY_POLE, 1 << 30, INT32_MAX):
        assert not check_mint(1, 0, height)
        assert destroy_cost_atoms(height) > 0
        assert min_burn_for_one_atom(height) >= 1
    return True


def no_overflow_in_uint128():
    """Both sides of the predicate stay inside unsigned __int128 for every
    reachable input: R is bounded by the cap, B by MAX_MONEY, h by int32."""
    r_max = res_max_supply_atoms()
    left = r_max * (RES_DESTROY_POLE + INT32_MAX) ** 2
    right = MAX_MONEY << RES_DESTROY_SHIFT
    assert left <= UINT128_MAX, left.bit_length()
    assert right <= UINT128_MAX, right.bit_length()
    return left.bit_length(), right.bit_length()


def cap_is_closed_form():
    """The closed-form cap agrees with a direct block-by-block summation over
    a prefix, and the asymptote is approached from below and never crossed."""
    cap = res_max_supply_atoms()
    total = 0
    for height in range(0, 1 << 18):
        total += max_mint_for_burn(ORU_FLAT_BLOCK_SUBSIDY, height)
    closed = cumulative_res_at(1 << 18)
    assert total <= cap
    # closed form tracks the summation to well under a part in 10^4
    assert abs(total - closed) * 10_000 < closed
    assert cumulative_res_at(1 << 40) < cap
    return total, closed


def run_self_checks():
    predicate_shift_is_exact()
    predicate_matches_destroy_cost()
    genesis_destroy_cost_is_exact()
    destroy_cost_strictly_increasing()
    no_free_mint_anywhere()
    no_overflow_in_uint128()
    cap_is_closed_form()
    return True


# --------------------------------------------------------------------------
# 4. Reporting
# --------------------------------------------------------------------------
MILESTONE_YEARS = [1, 4, 10, 16, 32, 50, 100, 250, 500, 638, 1000, 2000]


def year_to_height(year):
    return int(year * BLOCKS_PER_YEAR)


def schedule_rows():
    cap = res_max_supply_atoms()
    rows = []
    for year in MILESTONE_YEARS:
        height = year_to_height(year)
        cum_res = cumulative_res_at(height)
        rows.append({
            "year": year,
            "height": height,
            "destroy_cost_oru": destroy_cost_atoms(height) / COIN,
            "cumulative_oru": cumulative_oru_at(height) // COIN,
            "cumulative_res": cum_res / RES_COIN,
            "share_of_cap": cum_res / cap,
            "min_burn_atoms": min_burn_for_one_atom(height),
        })
    return rows


def report():
    run_self_checks()
    cap = res_max_supply_atoms()
    left_bits, right_bits = no_overflow_in_uint128()
    terminus = natural_terminus()

    print("RFC-020 — continuous RES destroy cost, flat ORU emission")
    print("=" * 78)
    print("all self-checks passed")
    print()
    print("Parameters")
    print(f"  ORU_FLAT_BLOCK_SUBSIDY   {ORU_FLAT_BLOCK_SUBSIDY:>22,} atoms "
          f"= {ORU_FLAT_BLOCK_SUBSIDY//COIN} ORU = 2^7")
    print(f"  ORU_GENESIS_BLOCK_SUBSIDY{ORU_GENESIS_BLOCK_SUBSIDY:>22,} atoms  (unchanged)")
    print(f"  RES_DESTROY_COST_GENESIS   {RES_DESTROY_COST_GENESIS:>22,} atoms "
          f"= {RES_DESTROY_COST_GENESIS//COIN} ORU = 2^10")
    print(f"  RES_DESTROY_POLE           {RES_DESTROY_POLE:>22,} blocks = 2^25 "
          f"= {RES_DESTROY_POLE/BLOCKS_PER_YEAR:,.0f} years")
    print(f"  RES_DESTROY_SHIFT          {RES_DESTROY_SHIFT:>22}  (forced: 50 - 10)")
    print(f"  RES_MAX_SUPPLY           {cap:>22,} atoms "
          f"= {cap//RES_COIN:,} RES = 2^22")
    print()
    print("Consensus rule")
    print("  R * (2^25 + h)^2 <= B << 40")
    print(f"  worst-case left  side 2^{left_bits - 1}, right side 2^{right_bits - 1}, "
          f"unsigned __int128 limit 2^128")
    print()
    print("Cap identity")
    print("  cap = (S / P0) * H = 2^7 / 2^10 * 2^25 = 2^(7 - 10 + 25) = 2^22")
    print()
    print("Continuity with RFC-019")
    print(f"  genesis destroy cost            {destroy_cost_atoms(0)//COIN:>6} ORU/RES   "
          f"(RFC-019: 1024 — identical)")
    print(f"  min burn for 1 RES atom  {min_burn_for_one_atom(0):>6} ORU atoms "
          f"(RFC-019: 1024 — identical)")
    print()
    print("Natural terminus (no horizon constant exists)")
    print(f"  height {terminus:,} = year {terminus/BLOCKS_PER_YEAR:,.0f}")
    print()
    print("Schedule (helper column is floor(cap * h / (h + 2^25)), not chain supply)")
    hdr = (f"  {'year':>6} {'height':>14} {'ORU/RES':>13} {'cum ORU':>18} "
           f"{'helper RES':>13} {'% helper':>9} {'min burn':>10}")
    print(hdr)
    for row in schedule_rows():
        print(f"  {row['year']:>6} {row['height']:>14,} {row['destroy_cost_oru']:>13,.2f} "
              f"{row['cumulative_oru']:>18,} {row['cumulative_res']:>13,.2f} "
              f"{row['share_of_cap']:>8.2%} {row['min_burn_atoms']:>10,}")
    print()
    print("rho times the integral constant RES_MAX_SUPPLY")
    print("  This product is not chain issuance and not the sum of per-block floors.")
    for rho in (0.05, 0.20, 0.50, 1.00):
        print(f"  rho = {rho:>5.0%}   {rho * cap / RES_COIN:>14,.1f} RES  (constant x rho)")
    print()
    print("Wallet destroy-cost margin (replaces the RFC-019 epoch-boundary hack)")
    for margin in (100, 1_000, 10_000, 100_000):
        cost = ((RES_DESTROY_POLE + margin) / RES_DESTROY_POLE) ** 2 - 1
        print(f"  constructing {margin:>7,} blocks ahead costs {cost:>12.8%} extra ORU")


def markdown():
    run_self_checks()
    cap = res_max_supply_atoms()
    print("| Year | Height | ORU per RES | Cumulative ORU | "
          "Cumulative RES | % of cap |")
    print("|-----:|-------:|------------:|---------------:|"
          "---------------:|---------:|")
    for row in schedule_rows():
        print(f"| {row['year']} | {row['height']:,} | {row['destroy_cost_oru']:,.2f} | "
              f"{row['cumulative_oru']:,} | {row['cumulative_res']:,.0f} | "
              f"{row['share_of_cap']:.2%} |")
    print()
    print("| rho | rho times RES_MAX_SUPPLY (not chain issuance) |")
    print("|----:|-----------------------------------------------:|")
    for rho in (0.05, 0.20, 0.50, 1.00):
        print(f"| {rho:.0%} | {rho * cap / RES_COIN:,.1f} RES |")


def as_json():
    run_self_checks()
    left_bits, right_bits = no_overflow_in_uint128()
    return {
        "parameters": {
            "ORU_FLAT_BLOCK_SUBSIDY": ORU_FLAT_BLOCK_SUBSIDY,
            "ORU_GENESIS_BLOCK_SUBSIDY": ORU_GENESIS_BLOCK_SUBSIDY,
            "RES_DESTROY_COST_GENESIS": RES_DESTROY_COST_GENESIS,
            "RES_DESTROY_POLE": RES_DESTROY_POLE,
            "RES_DESTROY_SHIFT": RES_DESTROY_SHIFT,
            "RES_MAX_SUPPLY": res_max_supply_atoms(),
        },
        "predicate": "R * (2^25 + h)^2 <= B << 40",
        "overflow_bits": {"left": left_bits, "right": right_bits, "limit": 128},
        "natural_terminus_height": natural_terminus(),
        "genesis_destroy_cost_atoms": destroy_cost_atoms(0),
        "genesis_min_burn_atoms": min_burn_for_one_atom(0),
        "schedule": schedule_rows(),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--markdown", action="store_true")
    args = parser.parse_args()
    if args.json:
        json.dump(as_json(), sys.stdout, indent=2)
        sys.stdout.write("\n")
    elif args.markdown:
        markdown()
    else:
        report()
    return 0


if __name__ == "__main__":
    sys.exit(main())
