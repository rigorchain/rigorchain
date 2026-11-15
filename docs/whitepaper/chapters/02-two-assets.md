# Chapter 2 — The Rules That Implement the Split

Chapter 1 assigned the three vertices to two assets. This chapter writes
that split as frozen rules. The claim is still architectural: the protocol
fixes a **transform**, not a price and not a participation rate.

```
PoW discovers and orders
        ↓
ORU enters circulation (coinbase, 128 per block)
        ↓
ORU is destroyed (OP_CONVERGE)
        ↓
the same body transaction MAY mint RES
```

Proof-of-work provides a publicly verifiable height ordering with
increasing rewrite cost. It
**does not** create RES. No Convergence → no new RES.

---

## 2.1 One mechanism, not two isolated tickers

For Chapter 1’s triangle to hold under this architecture, the two assets
are tied by a one-way transform. Free convertibility would weaken the
intended separation between the two monetary functions. No coupling at
all would again detach the miner budget from the scarce stock.

The protocol admits only this path:

```
work → ORU
ORU destroy → (may) RES
RES → ORU    illegal
```

`destroy_cost(h)` is a protocol destroy requirement, not a market quote.
While minting is open, a secondary RES/ORU ratio above that requirement
is bounded by mint arbitrage; a ratio below it simply stops minting. The
protocol does not publish a quote. It publishes the destroy cost at the
confirming height.

| Role | Asset | Frozen job |
|------|-------|------------|
| Medium of exchange, fees, miner cash-flow | **ORU** | Circulate; permanent subsidy; sole Convergence input |
| Designed store-of-value function | **RES** | Finite stock; created only by destroying ORU; never reverse-converted |

Users **mine ORU**. Users **destroy ORU**. The same transaction **may**
mint RES. That sentence must not be shortened to “mine RES”.

---

## 2.2 ORU — rules for the flow

ORU carries the vertex that is designed to be paid indefinitely: the
miner budget. It is also the everyday medium of exchange within the
protocol. Issuance is therefore designed to be predictable, not to
halve to zero, and not to carry a declared hard cap — a cap would lock
the store-of-value story back onto the flow asset.

```
subsidy(0) = 0
subsidy(h) = 128 ORU          for every h ≥ 1
```

| Item | Frozen rule | Why |
|------|-------------|-----|
| Script path | Witness v0 | Classification is identity, not a registry |
| Unit | `1 ORU = 100 000 000` atoms | Same decimals as RES; integer arithmetic |
| Genesis subsidy | `0` | Height 0 pays no subsidy |
| Every later block | `128 = 2⁷` ORU | Protocol floor for miner payment, independent of RES demand and of fees |
| Halving | None | Halving would return security payment to a fee-market outcome |
| Declared cap | None | Hardness is designed to be endogenous in `ρ`, not a number written at genesis |

Supply law:

```
S_ORU(h+1) = S_ORU(h) + R(h+1) − C(h+1)
```

`R(h)` is that block’s subsidy. `C(h)` is that block’s Convergence
destroy. Destroyed ORU leaves circulation. It does not become RES 1:1.

The inflation **rate** decays as `1/t`: about 1.00% at year 100, 0.100%
at year 1 000, monotonically toward zero and never reaching it. Residual
inflation **level** is not a schedule. It is `1 − ρ`. Under the protocol
model, `ρ = 100%` — every newly emitted ORU destroyed for RES —
corresponds to constant circulating ORU supply. A fixed cap does not
express this security-payment objective. That is why ORU does not get
one.

Minimum destroy: a positive atom (`nValue > 0`) is a legal plain
destroy. The destroy that mints one RES atom at genesis is 1 024 ORU
atoms.

ORU is meant to move. Spending it is not spending the scarce stock.

---

## 2.3 RES — rules for the stock

RES is designed as the store-of-value asset within the protocol. It
carries the vertex that is intended to remain scarce. It is therefore
not mined, not issued by governance, not swapped 1:1 for ORU, and not
convertible back. It is born only in a body transaction that destroys
ORU and mints Witness v1 RES in the same transaction.

Consensus never evaluates the display formula. It evaluates the same
relation with the division cleared:

```
R > 0  →  R · (2²⁵ + h)² ≤ B << 40
R = 0  →  B unconstrained              a plain destroy stays legal
```

`R` is RES atoms minted, `B` is the ORU atoms in the single
`OP_CONVERGE` output, `h` is the confirming height. Display form:

```
destroy_cost(h) = 1024 · (2²⁵ + h)² / 2⁵⁰
destroy_cost(0) = 1024 ORU per whole RES
```

`2⁵⁰` is the curve normalization denominator in `destroy_cost(h)`. It
is not a consensus shift. `2⁴⁰` in `B << 40` is the integer consensus
scaling factor after `1024 = 2¹⁰` is cancelled. It is not the
`destroy_cost` denominator. A display helper may floor the quotient
by dividing by `2⁵⁰`; that rounding/floor does not enter consensus.

`R = 0` is a legal plain destroy: ORU is removed and no RES is minted.
When `R > 0`, a failing inequality invalidates the whole transaction.

| Item | Frozen rule | Why |
|------|-------------|-----|
| Script path | Witness v1 | Isolated from ORU classification |
| Unit | `1 RES = 100 000 000` atoms | Aligned with ORU |
| Birth | Body-tx destroy-to-mint; never coinbase | Mining pays the flow, not the stock |
| Receiver | The transaction’s RES outputs, **not** the miner | Miners sell order and security; they do not pre-issue the reserve |
| Premine / admin mint | None | Entry must be a verifiable ORU destroy |
| Reverse conversion | Illegal | Otherwise the intended separation weakens |
| Per-block mint cap | None | Demand should be absorbable in a single block |
| Mint maturity | 100 blocks | Reorg protection |
| Ceiling | `2²² = 4 194 304` RES | Derived; see 2.4 |

```
S_RES(0) = 0
S_RES(h+1) = S_RES(h) + body_mint_atoms(h+1)
S_RES(h) ≤ 419 430 400 000 000 atoms
```

The cost is **strictly increasing** in height. That is not decoration.
It does two jobs. First, it writes Chapter 1’s duration-proportional
payment as a height function. This creates a duration-sensitive cost
schedule: earlier participation receives a lower protocol-defined
destroy cost because it commits capital earlier in the height timeline.
An earlier mint therefore pays less per RES for a longer remaining span
of protection. Second, it makes the ceiling need no consensus counter —
see the next section.

Whether a mint occurs is demand-driven. The cost **if** it occurs is a
pure function of height, not a market quote, and it does not move with
difficulty.

---

## 2.4 Where the parameters come from

The primary issuance constants are selected as powers of two to
simplify arithmetic verification. The cap is the product of three of
them, so the derivation of the cap follows directly from the exponent
relationships:

```
RES_MAX_SUPPLY = ∫_0^∞ S / destroy_cost(h) dh
               = (S / P0) · H
               = 2⁷ / 2¹⁰ · 2²⁵
               = 2²²
               = 4 194 304 RES
```

`S = 128`, `P0 = 1024`, `H = 2²⁵ = 33 554 432` blocks (about 638 years at
600-second blocks). The exponent 2 makes that integral equal `(S / P0) · H`.
The discrete sum is not that integral. Height 0 creates no ORU, and the
integer mint rule admits only `floor((B << 40) / (2²⁵ + h)²)`. The constant
is an upper bound on that schedule. It is not an amount the chain can mint.

`RES_DESTROY_SHIFT = 40` is not chosen independently. It is the
integer consensus scaling factor (`B << 40`), forced by cancelling
genesis cost `1024 = 2¹⁰` against the display denominator `2⁵⁰`. It
is not the `destroy_cost` denominator:

```
R · 1024 · (2²⁵ + h)²  ≤  B · 2⁵⁰
R · (2²⁵ + h)²         ≤  B · 2⁴⁰
```

so `40 = 50 − log₂(1024)`. A different genesis cost moves the scaling
factor and nothing else. `2⁵⁰` remains the display-curve denominator.

ORU cannot be destroyed before it exists. `destroy_cost` is strictly
increasing, so destroying a subsidy later admits less RES than destroying
it at the height it is created. That schedule starts at height 1, because
height 0 creates no ORU. Its real-valued sum is strictly below the integral,
and the integer floors are lower still. Block validation does not consult a
running RES counter.

The node helper `ResCumulativeMaxAtHeight(h)` returns
`floor(RES_MAX_SUPPLY · h / (h + 2²⁵))` for `h > 0`. At `h = 2²⁵` that helper
equals half the constant, and the displayed destroy cost is 4 096 ORU per
RES. The helper is not the sum of the per-block floors, and a fraction `ρ`
of emitted ORU does not mint `ρ` times the constant.

Minting ends when a whole block’s subsidy no longer buys a single RES
atom. Integer arithmetic decides the height: `118 599 277 598` (about
year 2 254 957). That is a consequence of the parameters, not a
parameter.

---

## 2.5 The schedule is an upper bound, not a calendar promise

The table below is `ResDestroyCostAtHeight` and `ResCumulativeMaxAtHeight`,
with cumulative RES rounded to a whole RES. Cumulative ORU is `height · 128`.
These columns are not the sum of the per-block integer mint maxima.

| Year | Height | ORU / RES | Cumulative ORU | Cumulative RES | % of cap |
|-----:|-------:|----------:|---------------:|---------------:|---------:|
| 1 | 52 594 | 1 027.21 | 6 732 032 | 6 564 | 0.16% |
| 4 | 210 379 | 1 036.88 | 26 928 512 | 26 134 | 0.62% |
| 10 | 525 949 | 1 056.35 | 67 321 472 | 64 729 | 1.54% |
| 16 | 841 518 | 1 076.01 | 107 714 304 | 102 616 | 2.45% |
| 32 | 1 683 037 | 1 129.30 | 215 428 736 | 200 331 | 4.78% |
| 50 | 2 629 746 | 1 190.80 | 336 607 488 | 304 828 | 7.27% |
| 100 | 5 259 492 | 1 370.17 | 673 214 976 | 568 350 | 13.55% |
| 250 | 13 148 730 | 1 983.78 | 1 683 037 440 | 1 180 857 | 28.15% |
| 500 | 26 297 460 | 3 258.04 | 3 366 074 880 | 1 842 875 | 43.94% |
| 638 | 33 555 558 | 4 096.14 | 4 295 111 424 | 2 097 187 | 50.00% |
| 1 000 | 52 594 920 | 6 750.01 | 6 732 149 760 | 2 560 659 | 61.05% |
| 2 000 | 105 189 840 | 17 507.75 | 13 464 299 520 | 3 179 938 | 75.82% |

The helper at the year-1 height rounds to 6 564 RES. That figure is not a
count of RES the chain has minted. In early
markets with limited liquidity, secondary price discovery may be
influenced by the protocol mint requirement — the intended transmission
of RES demand into ORU destroy. A market too thin to discover any ratio
does not start that transmission. The RFC asserts the mechanism, not
the threshold.

Destroy cost changes by about 0.000 089% per block. Wallets construct at
the confirm height plus a flat margin; an over-generous margin is a
rounding error, not a strategy. This repository's wallet takes that
margin as 144 blocks after the current tip. See
[Wallet Convergence confirmation margin](../../wallet/WALLET_CONVERGENCE_HEIGHT_PAD.md).

Selected pins (atoms, display only):

| Height | Atoms | ORU / 1 RES |
|-------:|------:|------------:|
| 0 | 102 400 000 000 | 1 024 |
| 210 000 | 103 685 749 152 | 1 036.857 491 52 |
| 420 000 | 104 979 520 049 | 1 049.795 200 49 |

---

## 2.6 The second sink — RFC-018

RFC-020 specifies the protocol cost of RES **creation**. RFC-018 keeps
absorbing ORU when RES **moves**. The two sinks are not collapsed into
one “supply” number.

RES transfers still pay network fees on the ORU leg. Half of that fee is
unclaimable in coinbase, never enters the UTXO set, and is therefore
destroyed:

```
destroy_RES_transfer = Σ floor(fee(tx) · 1 / 2)
```

The other half remains claimable, so miners still want to include RES
transfers. Pure ORU spends, Convergence itself, and body mints do not
take this half — Convergence is already destroyed on the `OP_CONVERGE`
path.

Four ORU quantities that remain separate:

| Quantity | What it is | What it is not |
|----------|------------|----------------|
| ORU ledger supply | Issued subsidy minus Convergence `C(h)` | Circulating UTXO, fee destroy, or a wallet balance |
| Circulating UTXO | Spendable Witness v0 ORU still in the UTXO set | The ledger path, or either destroy |
| Convergence destroy | ORU in an unspendable `OP_CONVERGE` | Fee destroy |
| Fee destroy | Unclaimed half of RES-transfer ORU fees | Convergence `C(h)`, or a destroy UTXO |

Explorers may display each separately. None is a global asset-state
table. Consensus does not keep a “total supply” registry.

---

## 2.7 Where each asset is born

| | **ORU** | **RES** |
|--|---------|---------|
| Job | Hashpower payment, fees, Convergence input | Independent scarce stock |
| Classification | Witness v0 | Witness v1 |
| Ledger origin | Coinbase subsidy `R(h)` | Body-tx destroy-to-mint |
| Per block | 128 ORU (`R(0) = 0`) | Any `R` the predicate admits |
| Who receives it | The miner | The minting transaction’s RES outputs, not the miner |
| Cap | No declared hard cap | Derived `2²²` RES |
| Calendar | Subsidy is a height function | Cost is a height function; occurrence of a mint is not |

**Protocol-enforced:** a mint MUST satisfy the predicate or the
transaction is invalid; `S_RES` MUST NOT exceed the derived ceiling
(runtime defence; §2.4 shows it is unreachable).

**Not guaranteed:** that anyone destroys ORU; that the ceiling is
filled; that a secondary market exists; that `ρ` lands in any band.

---

## 2.8 What this chapter does not claim

- It does not target an external price for either asset.
  `destroy_cost(h)` is not a quote.
- It does not assert that `ρ` will be high, or that the schedule will be
  walked.
- It does not assert that a usable RES/ORU secondary market exists at
  launch.
- It does not collapse the four ORU quantities into one “float” story.
- It does not claim C++ equivalence to the kernel model. The Coq model
  proves selected invariants (including the ORU fold identity
  `n · 128`). It does not prove `ConnectBlock`.

**The rules fix the transform. Whether demand arrives is not a rule.**
Chapter 3 states how the two identities exist on the ledger as native
UTXO scripts — not as a registry, and not as an overlay.
