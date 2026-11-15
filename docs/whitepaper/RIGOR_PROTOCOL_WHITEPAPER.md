# RIGOR Protocol Whitepaper

**A Dual-Asset Proof-of-Work Protocol with Convergence and Independent Verification**

| Field | Value |
|-------|-------|
| Document status | Public protocol narrative (not consensus SSOT) |
| Date | 2026-09-15 |

This document is the public protocol narrative. It covers purpose,
consensus design, security assumptions, and implementation limits.
Live admit-or-reject is defined by consensus code together with
RFC-020 and RFC-018. This paper does not rewrite those rules.

Protocol SSOT: [`../protocol/RFC-020.md`](../protocol/RFC-020.md),
[`../protocol/RFC-018.md`](../protocol/RFC-018.md).
Implementation facts: [`../developer/CODE_SSOT_PROTOCOL_FACTS.md`](../developer/CODE_SSOT_PROTOCOL_FACTS.md).

Public mathematics is `docs/whitepaper/RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md`.

**Core identity:**

```
Dual Asset
    +
Convergence
    +
Public mint predicate
    +
Independent local verification
    +
Visible mining participation
```

**Official architecture formula:**

```
RIGOR =
    2 Native Assets (ORU, RES)
  +
    Closed monetary script templates (v0 ORU, v1 RES, mint, converge)
  +
    UTXO Local Validation
  +
    Ed25519 Authentication
  +
    OpuHash (SHA3-256 / RGRPOW01) + ASERT PoW
  +
    Consensus-defined RES issuance
  +
    Fee destroy (RFC-018)
  +
    Coq protocol-model verification
  +
    No Global Asset State
```

**Architecture identity:**

RIGOR is a two-asset UTXO protocol with a closed script-compliance surface.

It combines:

- ORU (Operational Resource Unit) and RES (Reserve End State) native assets
- consensus-defined RES issuance
- fee destroy on selected ORU flows (RFC-018)
- Ed25519 authentication
- OpuHash (SHA3-256 / `RGRPOW01`) Proof-of-Work
- UTXO-local validation

without:

- token registry
- account ledger
- contract storage
- global asset state

**Official asset names:**

```
ORU (Operational Resource Unit)
    the operational asset produced by Proof-of-Work.
RES (Reserve End State)
    the reserve-oriented asset created through irreversible ORU destruction.
```

The RIGOR protocol model contains machine-checked consensus invariants.
This does not constitute a proof that the production C++ implementation
is equivalent to the model.

```
formal verification of protocol model
        ≠
C++ implementation equivalence
        ≠
running binary correctness
```

---

## Abstract

RIGOR explores an alternative architecture for a long-standing design
question in proof-of-work systems: how a single native asset can
simultaneously support transaction utility, long-term scarcity, and
miner security incentives.

A single-asset design assigns multiple economic roles to the same unit:
circulate as a medium of exchange, remain scarce as a store of value,
and pay miners for security. These roles may involve different users,
different time horizons, and different incentive preferences. Cheap
transfers want circulation. Scarcity wants withdrawal from circulation.
Security wants a miner budget that does not vanish when holders stop
transacting. The result is a **security-budget tension** — an
architectural challenge, not a claim that any single-asset design has
been disproved.

**RIGOR** separates these roles at the consensus asset layer.

- **ORU (Operational Resource Unit)** is the operational asset produced
  by Proof-of-Work. It is the utility asset: fees, miner subsidy, and
  Convergence entry weight. It has a flat subsidy of **128 ORU** per
  block so miner payment does not depend on inflating the reserve.
- **RES (Reserve End State)** is the reserve-oriented asset created
  through irreversible ORU destruction. Its maximum supply is
  **derived**: `RES_MAX_SUPPLY = 419430400000000` atoms (`4194304 RES`).
  It is not created by mining subsidy, not by governance, and not by a
  1:1 swap. New RES appears only in a **body transaction** that destroys
  ORU (`OP_CONVERGE`) and mints Witness v1 RES in the **same
  transaction** at protocol cost `destroy_cost(h)`. RES is created under
  **deterministic consensus rules** when those conditions hold. It is
  not mined.

**Convergence** is the core mechanism. A user destroys ORU in an
`OP_CONVERGE` output. The same transaction MAY mint RES atoms `R` when
`R > 0` and `R · (2²⁵ + h)² ≤ B << 40` at the confirming height.
`destroy_cost(0) = 102400000000` (1024 ORU per 1 RES).
`destroy_cost(h)` is a **protocol cost**, not a market quote. The
protocol defines the admission rule; market demand determines whether
participants use it.

**Independent verification** means a stranger does not accept a website
or explorer verdict. Public data is recomputed locally. Nodes evaluate
the same public data and reach the same consensus decision when the
data is identical. A product page named Zero Trust is a selected-field
recomputation tool. It is not a substitute for a full node, and it is
not a claim that the chain is “formally verified.”

This paper explains *why* the split exists, *how* Convergence works, how
miners participate (including the **Windows** path), and how verification
is layered. It does not claim protocol-to-binary equivalence or mainnet
completion. It does not make claims about market adoption, valuation, or
future security outcomes.

---

## Contents

1. Why RIGOR Separates Two Monetary Functions (ORU and RES)
2. The Rules That Implement the Split
3. Native UTXO Identities
4. Convergence: The One-Way Bridge
5. Checkable Mathematics
6. Proof-of-Work: Shared Height, Paid in ORU
7. Local Verification
8. The Formal Boundary
9. Product Surfaces: A Display Remains a Display
10. Security Model and Threat Boundaries
11. Design Trade-offs
12. Deployment and Evolution Policy

Appendix — Protocol Constants and Engineering Reference

References and Design Context

Appendix D — Convergence Mathematical Design Note
([`RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md`](./RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md))

---

# Chapter 1 — Why RIGOR Separates Two Monetary Functions

This chapter states **why RIGOR separates two native assets** before
later chapters state how the protocol works. The claim is a protocol
design rationale, not a market forecast. The expanded form is
[`chapters/01-single-asset-problem.md`](./chapters/01-single-asset-problem.md).

A public proof-of-work chain must do three jobs at once:

```
Medium of Exchange
        +
Store of Value
        +
Security Incentive
```

A typical single-asset design, especially one with a declared finite
supply, asks **one** native unit to perform all three. Those jobs are not
the same job. Their users do not share a time horizon, a fee tolerance, or
a reason to transact. The result is a **security-budget tension**: after
the subsidy ends, the users who benefit most from long-term security are
not necessarily the users generating transaction fees.

### Design context

RIGOR is motivated by a broader cryptoeconomic observation: a single
monetary asset may face competing pressures when required to serve as
medium of exchange, store of value, and security budget at once.
Related design context includes Bitcoin (Nakamoto, 2008), Nervos CKB
Cryptoeconomics RFC-0015, and later fee security-budget discussions.
RIGOR shares the observation that monetary functions may create
conflicting incentives when assigned to one asset. RIGOR applies this
observation through a separate dual-asset UTXO design. It does not
copy those models.

## 1.1 Two users, two motives

A **medium of exchange** is the unit people use to move value. MoE users
treat the network as a peer-to-peer transfer system. They care about fee
cost in an external unit and confirmation. Exposure to network-security
risk is often momentary. Payment services can buy, send, and sell in one
motion. For them, the asset must **circulate**. They have little incentive
to fund a security budget whose benefit they barely consume.

A **store of value** is the unit people hold because it is scarce and the
rules are stable. SoV users treat the network as the protocol that
protects a scarce native stock across time. Transfer is only the on-ramp
and off-ramp. They care about long-term security and decentralisation.
Transaction costs can be amortised over long holding periods. These users
have the strongest incentive to pay for continuous protection — but they
transact infrequently.

| Role | Time horizon | What they want | What they contribute to miner payment |
|------|--------------|----------------|---------------------------------------|
| Medium of exchange | Minutes to hours | Cheap, frequent transfers | Fees — and they resist high fees |
| Store of value | Years | Scarcity and permanent protection | Almost no transactions, so almost no fees |
| Miners | Every block, forever | Predictable payment for hashpower | Protection of the *entire* history, not only the current block |

A single unit is asked to be, at once: the thing people spend, the thing
people hold, and the thing that pays for hashpower. Making it “something
people are willing to spend” pulls against making it “something people
are willing to hold and pay for hashpower under the stated miner-payment
mechanism.”

## 1.2 Finite supply locks the store-of-value story

A declared hard cap does not merely limit issuance. It **selects a
narrative**. Once the market treats the unit as a scarce reserve,
long-term holders may prefer holding over spending when expected future
appreciation exceeds transactional utility. Velocity falls. The fee
market that was supposed to replace the subsidy then tends not to appear
at the scale the security budget requires.

The mechanism is not classical Gresham. Classical Gresham requires a
legally fixed exchange rate between two monies. Here there is one unit
and no legal mint ratio. Circulation, if suppressed, comes from
**appreciation expectation and incentive preference**: the same object
may be more valuable held than spent.

The deeper mismatch is temporal. Miner hashpower protects the
immutability of every prior block, not only today’s. A store-of-value
user consumes that protection for as long as they hold. A one-shot
inclusion fee is a poor instrument for a perpetual service. Asking SoV
users to pay for continuous security with a payment that only occurs when
they *stop* holding is incentive-incompatible.

## 1.3 Two paths, one constraint

Bitcoin is the canonical single-asset, finite-supply design. It has been
asked to become a medium of exchange and, separately, a store of value.
Both paths face unresolved trade-offs under a single finite-supply asset
model. They are two faces of the same constraint.

**Path A — Bitcoin as MoE.** Users need not hold the asset; they price
the external cost of the fee. Fixed block time and size make successful
adoption raise fees and create a competitiveness trade-off. Faster or
larger blocks buy cheaper inclusion at the cost of security and
decentralisation.

**Path B — Bitcoin as SoV.** Holders will tolerate fees. They will not
tolerate an insecure or captured chain. After the last subsidy, miners
are paid only by fees. A successful SoV culture is exactly a culture that
does not transact. Whether fees replace issuance is an open question. It
is not a protocol rule. It is an unresolved empirical question.

The large-block attempt to force Path A does not escape Path B. Supply is
still finite, so the market still attaches some reserve premium. The
result is unused extra block space, a valuation discount relative to the
scarce-reserve story, and the same security-budget tension.

```
finite supply + one asset
        │
        ├─ emphasise scarcity  →  hoarding  →  thin fees  →  post-subsidy security gap
        │
        └─ emphasise throughput →  weaker SoV premium
                                   + still-finite supply
                                   →  neither a working fee market
                                   nor a clean reserve
```

A single finite-supply asset faces a structural tension: making people
willing to spend it and willing to hold it for decades pulls in opposite
directions.

## 1.4 Who pays the miners

Network security is a **flow** (continuous protection of history). A fee
is a **spot** payment for inclusion. Funding the first with the second is
a category error.

Inflation-based block reward is the incentive-compatible instrument for a
store-of-value network: holders pay, in proportion to duration, by
accepting dilution. Placed on the **same** unit that is supposed to be
the store of value, that instrument weakens the reserve story. Placed on
no unit at all, it leaves miners unpaid after the subsidy.

```
            cheap circulation
                 / \
                /   \
               /     \
        finite      perpetual
        scarcity    miner budget
```

Under this constraint, a design may select two vertices. A
finite-scarcity single-asset design selects finite scarcity and treats
the third vertex as a fee-market outcome. Perpetual issuance on the same
ticker selects the miner budget and forgoes a terminal reserve. RIGOR
does not select two vertices on one ticker. It assigns the vertices to
two assets and couples them.

## 1.5 The split — RFC-020

**ORU (Operational Resource Unit)** is the operational asset produced
by Proof-of-Work. It is the medium of exchange and the miner-payment
asset. Flat subsidy: `subsidy(0) = 0`, `subsidy(h ≥ 1) = 128` ORU. No
halvings. ORU has no fixed supply cap because its primary role is
continuous security payment rather than terminal scarcity. It is a
**flow asset**. Inflation *rate* decays as `1/t`. Residual inflation
*level* is `1 − ρ`, where `ρ` is the fraction of emitted ORU ever
destroyed to create RES. Hardness is a function of demand, not of a
number written at genesis. Users can spend ORU without sacrificing a
reserved position.

**RES (Reserve End State)** is the reserve-oriented asset created
through irreversible ORU destruction. It is designed as the
store-of-value asset within the protocol. It is not mined. There is no
reverse conversion. It is created only in a body transaction that
destroys ORU (`OP_CONVERGE`) and mints Witness v1 RES when
`R > 0 → R · (2²⁵ + h)² ≤ B << 40`. Display form:
`destroy_cost(h) = 1024 · (2²⁵ + h)² / 2⁵⁰` ORU per whole RES.
`2⁵⁰` is the curve normalization denominator, not a consensus shift.
`2⁴⁰` is the integer consensus scaling factor, not the `destroy_cost`
denominator. Display rounding/floor do not enter consensus. The cost
is strictly increasing, so an earlier mint pays less per RES for a longer
remaining span of protection — duration-proportional payment, encoded as
a height function. The ceiling `2²² = 4 194 304` RES is the closed-form
integral of the per-block mint ceiling. It needs **no consensus
counter**: any delay yields strictly less RES. Realised stock is
`ρ · 2²²`. The issuance curve has no artificial expiration horizon and
asymptotically approaches the supply limit.

```
work                      →  ORU subsidy (128 / block)
RES demand at the ceiling →  ORU destroy          (RFC-020)
RES transfers             →  ORU fee destroy      (RFC-018)
ρ                         →  realised RES stock and residual ORU inflation
```

`destroy_cost(h)` is a protocol destroy requirement, not a market quote.

## 1.6 After the split

Hoarding is isolated on RES. ORU can develop a fee market without
competing with a reserve narrative on the same unit. The miner budget has
a protocol floor — 128 ORU every block — that does not depend on SoV
holders transacting. When RES demand exists at the ceiling, that demand
is an ORU sink. The protocol does not invent the demand. It converts
demand, when it exists, into destroyed ORU and a paid miner.

ORU alone is a work-payment asset with no scarce stock. RES alone is a
capped stock with no way to pay hashpower. The pair is not an optional
optimisation. It is an alternative protocol architecture that assigns
conflicting monetary functions to two coupled native assets.

This chapter does not target an external price, does not assert that `ρ`
will be high, and does not claim C++ equivalence to the kernel model.
Year-1 RES issuance is at most about 6 564 even at `ρ = 100%`. A market
too thin to discover any ratio cannot transmit demand into destroy. That
is an adoption risk, not a consensus hole.

RIGOR does not assume that protocol design alone creates market demand.
The protocol separates monetary functions and provides mechanisms that
translate demand, when it exists, into security-related flows.
Market adoption remains an external factor.

**RIGOR’s answer is not a market promise. It is an asset split at
consensus.** Chapter 2 states the rules that implement the split.

---

# Chapter 2 — The Rules That Implement the Split

Chapter 1 assigned the three vertices to two assets. This chapter writes
that split as frozen rules. The claim is still architectural: the protocol
fixes a **transform**, not a price and not a participation rate. The
expanded form is [`chapters/02-two-assets.md`](./chapters/02-two-assets.md).

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
increasing rewrite cost. It **does not** create RES. No Convergence →
no new RES.

## 2.1 One mechanism, not two isolated tickers

Free convertibility would weaken the intended separation between the
two monetary functions. No coupling at all would again detach the miner
budget from the scarce stock. The protocol admits only this path:

```
work → ORU
ORU destroy → (may) RES
RES → ORU    illegal
```

`destroy_cost(h)` is a protocol destroy requirement, not a market quote.
Users **mine ORU**. Users **destroy ORU**. The same transaction **may**
mint RES. That sentence must not be shortened to “mine RES”.

## 2.2 ORU — rules for the flow

```
subsidy(0) = 0
subsidy(h) = 128 ORU          for every h ≥ 1
S_ORU(h+1) = S_ORU(h) + R(h+1) − C(h+1)
```

No halvings. ORU has no fixed supply cap because its primary role is
continuous security payment rather than terminal scarcity. It is a
**flow asset**. The inflation **rate** decays as `1/t`. Residual
inflation **level** is `1 − ρ`. Under the protocol model, `ρ = 100%`
corresponds to constant circulating ORU supply. A fixed cap does not
express this security-payment objective.

The 128 is the miner-budget floor in the utility asset. It does not
depend on RES demand or on fees. Spending ORU is not spending the scarce
stock. A positive atom is a legal plain destroy. The destroy that mints
one RES atom at genesis is 1 024 ORU atoms.

## 2.3 RES — rules for the stock

RES is designed as the store-of-value asset within the protocol. It is
not mined, not issued by governance, not swapped 1:1, and not
convertible back. Consensus evaluates

```
R > 0  →  R · (2²⁵ + h)² ≤ B << 40
R = 0  →  B unconstrained
```

Display form: `destroy_cost(h) = 1024 · (2²⁵ + h)² / 2⁵⁰`,
`destroy_cost(0) = 1024` ORU per whole RES. `2⁵⁰` is the curve
normalization denominator, not a consensus shift. `2⁴⁰` in `B << 40`
is the integer consensus scaling factor, not the `destroy_cost`
denominator. Display rounding/floor do not enter consensus. The cost
is strictly increasing. This creates a duration-sensitive cost schedule: earlier
participation receives a lower protocol-defined destroy cost because it
commits capital earlier in the height timeline. An earlier mint
therefore pays less per RES for a longer remaining span of protection.
Receiver is the minting transaction’s RES outputs,
**not** the miner. Mint maturity 100 is reorg protection. There is no
per-block mint cap.

```
S_RES(0) = 0
S_RES(h+1) = S_RES(h) + body_mint_atoms(h+1)
S_RES(h) ≤ 419430400000000
```

Whether a mint occurs is demand-driven. The cost **if** it occurs is a
pure function of height.

## 2.4 Where the parameters come from

The primary issuance constants are selected as powers of two to
simplify arithmetic verification. The cap is the product of three of
them:

```
RES_MAX_SUPPLY = (S / P0) · H = 2⁷ / 2¹⁰ · 2²⁵ = 2²² = 4 194 304 RES
```

`RES_DESTROY_SHIFT = 40` is the forced integer consensus scaling
factor: `40 = 50 − log₂(1024)`. It is not the `destroy_cost`
denominator (`2⁵⁰`). Because
`destroy_cost` is strictly increasing, the closed-form integral is an
upper bound that **needs no consensus counter**. Delaying a destroy
yields strictly less RES. Realised stock is `ρ · 2²²`. Cumulative
through height `h` is `cap · h / (h + 2²⁵)`. The pole `h = 2²⁵` is the
median of the `ρ = 100%` distribution (year 638; about 4 096 ORU per
RES).

The `ρ = 100%` year-1 ceiling is about 6 564 RES. In early markets with
limited liquidity, secondary price discovery may be influenced by the
protocol mint requirement. A market too thin to discover any ratio does
not transmit demand into destroy.

## 2.5 The second sink — RFC-018

RFC-020 specifies the protocol cost of RES creation. RFC-018 destroys
`floor(fee / 2)` of the ORU fee on RES **transfers**. Pure ORU spends
and Convergence itself do not take this half. Four ORU quantities remain
separate: ledger supply (issued minus `C(h)`), circulating UTXO,
Convergence destroy, and fee destroy. Consensus does not keep a global
supply table.

## 2.6 Enforced vs not guaranteed

**Enforced:** the mint predicate; no RES→ORU; `S_RES` at or below the
derived ceiling. **Not guaranteed:** that anyone destroys ORU, that the
ceiling is filled, that a secondary market exists, or that `ρ` lands in
any band.

**The rules fix the transform. Whether demand arrives is not a rule.**
How the two identities exist on the ledger as native UTXO scripts — not
as a registry, and not as an overlay — is Chapter 3.

---

# Chapter 3 — Native UTXO: How the Two Identities Exist on the Ledger

Chapter 2 wrote the three vertices as frozen rules. This chapter states
how those two identities exist as ledger objects. The protocol fixes
**objects and classification**, not an off-chain registry and not a
colour on a single UTXO. The expanded form is
[`chapters/03-native-utxo.md`](./chapters/03-native-utxo.md).
Engineering audit:
[`chapters/RIGOR_MINIMAL_SCRIPT_ARCHITECTURE.md`](./chapters/RIGOR_MINIMAL_SCRIPT_ARCHITECTURE.md).

If ORU and RES were only annotations on one output, Chapter 1’s split
would not have happened. A registry can credit RES without a destroy. An
inscription can stick a store-of-value story back onto the medium of
exchange. The split is the same classification every full node reads
from the same bytes.

## 3.1 The ledger object is still a UTXO

```
TxOut = (nValue, scriptPubKey)
```

No account model as consensus truth. No parallel `asset` field. No
`(asset, value, script)` triple. Ownership changes only by spending
previous outputs and creating new ones. The same accepted block bytes
must produce the same validity decision.

## 3.2 Script classifies; consensus legitimates

`AssetTypeFromScriptPubKey` reads witness version and program shape:

```
script shape → ORU | RES | NONE | INVALID
```

**Script defines identity; consensus defines legitimacy.** Parsing
Witness v1 is not a legal mint. Birth of RES still requires
`OP_CONVERGE` in the same transaction and
`R > 0 → R · (2²⁵ + h)² ≤ B << 40`. Script alone does not authorize
asset creation.

## 3.3 Closed templates

Production has **no Script VM**. Bytes are classified. Spends go through
`VerifyRigorPredicate`.

| Shape | Asset | Spendable |
|-------|-------|-----------|
| Witness v0, 20-byte | ORU | Yes |
| Witness v1, 32-byte | RES | Yes |
| `<HASH160> 0xbd` | ORU (destroy) | No — not a UTXO |
| Zero-value `0x6a` | NONE | No |
| Anything else | INVALID | — |

Coinbase `0xbb` is not the RFC-020 mint path. In the production protocol
profile, P2PKH, P2SH, P2WSH, Taproot, and secp256k1 spend paths are not
accepted. **Witness v1 is not Bitcoin Taproot.**

Conservation is per asset pool: ORU inputs cannot fund RES outputs;
RES inputs cannot fund ORU outputs. The only one-way exception is
Chapter 2’s destroy-to-mint. The new RES is a new UTXO, not a colour
on the old output.

## 3.4 Not a registry, not an overlay

ORU and RES are not colored coins, inscriptions, Ordinals, Runes,
contract units, or wrapped assets. Neither is a secondary representation
of the other. The split happens in consensus, not in the explorer.

```
Less capability → less complexity → smaller attack surface
→ more feasible mathematical verification
```

The goal is maximum verifiability, not maximum programmability.

**Identity lives in the script. Legitimacy lives in consensus.**
How Convergence is verified as that one-way bridge is Chapter 4.

---

# Chapter 4 — Convergence: How the One-Way Bridge Is Verified

Chapter 3 gave two native identities. Without a public, deterministic
bridge they are two unrelated ledger objects. This chapter states how
every full node verifies that bridge. The expanded form is
[`chapters/04-convergence.md`](./chapters/04-convergence.md).

The protocol fixes **one visible destroy, and an integer predicate in
the same transaction**. It does not fix that anyone will destroy.

```
ORU inputs
        ↓
exactly one OP_CONVERGE output, amount B (destroyed, not a UTXO)
        ↓
Witness v1 RES outputs summing to R, same body transaction
        ↓
at confirming height h: R > 0 → R · (2²⁵ + h)² ≤ B << 40
```

No Convergence → no new RES.

## 4.1 Three requirements that do not come apart

If destroy and mint sit in different transactions, the ledger has a
window of credit. If destroy is inferred from
`inputs − outputs − fee`, it may become indistinguishable from ordinary
fee accounting. If a miner writes RES into coinbase, the bridge becomes
miner discretion.

1. **Destroy is visible** — `<HASH160> 0xbd`, value `B`, not inserted
   into the UTXO set, ORU inputs only, at most one positive Convergence
   output per transaction.
2. **Mint is same-transaction** — `R` is the sum of Witness v1 outputs
   in that body transaction. The miner who includes it receives no RES.
3. **The predicate is integer** — same `R`, `B`, `h` ⇒ same
   admit-or-reject. No unpublished rate.

`R = 0` is a legal plain destroy. Ordinary RES transfers cannot create
RES (`res_in ≥ res_out`). RES cannot convert back to ORU.

## 4.2 Not a 1:1 counter

A 1:1 swap would rename flow into stock and weaken Chapter 1’s
intended assignment of three vertices to two assets. The protocol cost
rises with height. The new RES is a new UTXO, not a colour on the old
output.

Pool policy cannot allocate RES. Changing a miner payout address cannot
create RES. Fairness within the consensus rule is defined as
deterministic evaluation of the same public inequality.

`destroy_cost(h)` is a protocol destroy requirement, not a market quote.
Display form is `1024 · (2²⁵ + h)² / 2⁵⁰`. The integer predicate is
the exact integer form of the mint condition, algebraically equivalent
to the display-cost condition. No rounding or truncation occurs.
Consensus never evaluates the display quotient. There is no per-block
mint cap.

## 4.3 When verification fails

`bad-txns-res-mint-underpaid` — destroy does not cover `R`.
`bad-txns-res-mint-no-burn` — RES output, no `OP_CONVERGE`.
`illegal-res-to-oru-conversion` — wrong direction, or spending the
destroy. Underpayment invalidates the whole transaction.

Mint maturity 100 is reorg protection, the same depth as coinbase.

**Destroy is visible. Mint is an integer predicate in the same
transaction.** Chapter 5 unfolds that predicate as checkable
mathematics. Algebraic derivation is in Appendix D:
[`RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md`](./RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md).
That Mathematical Design Note is explanatory documentation. It does
not introduce new consensus rules.

The Convergence curve defines deterministic protocol issuance cost.
It does not define market price, profitability, or future demand.
Differences in issuance cost across height do not constitute a protocol
guarantee of economic return.

---

# Chapter 5 — Checkable Mathematics

Chapter 4 fixed the verification act. This chapter unfolds the
predicate: display and consensus are one identity; the ceiling is an
integral; fairness is the same public inputs yielding the same
decision. The expanded form is
[`chapters/05-mathematical-fairness.md`](./chapters/05-mathematical-fairness.md).

## 5.1 Fairness is a public decision function

```
same (R, B, h)  →  same admit or reject
```

No fourth hidden parameter. No unpublished rate. No random term. Anyone
who re-evaluates a confirmed transaction must get the same yes or no as
the network. In this chapter, fairness means a deterministic and
publicly reproducible admit-or-reject. It does not mean equal mint
amount, equal timing, or equal economic outcome. Within the consensus
rule, that decision is a public, exact function of `(R, B, h)`.

## 5.2 The consensus scaling factor is forced

Display: `destroy_cost(h) = 1024 · (2²⁵ + h)² / 2⁵⁰`. `2⁵⁰` is the
curve normalization denominator, not a consensus shift. Consensus never
divides. A display helper may floor `/ 2⁵⁰`; that rounding/floor does
not enter consensus. The integer predicate is the exact integer form of
the mint condition, not an approximation. Clearing the division is an exact
integer-preserving equivalence:

```
R · 1024 · (2²⁵+h)²  ≤  B · 2⁵⁰
R · (2²⁵+h)²         ≤  B · 2⁴⁰
```

so `RES_DESTROY_SHIFT = 50 − log₂(1024) = 40`. `2⁴⁰` is the integer
consensus scaling factor, not the `destroy_cost` denominator. Change
genesis cost and leave the scaling factor, and the predicate no longer
expresses that curve. Consensus evaluates one multiplication, one
squaring, one shift. No rounding direction: `check_mint` is exact.
`destroy_cost(0)` is exactly 1024. No free mint is representable.

## 5.3 The ceiling is derived

`S = 2⁷`, `P0 = 2¹⁰`, `H = 2²⁵`. The exponent 2 makes

```
∫_0^∞ S / destroy_cost(h) dh = (S / P0) · H = 2²²
```

with no residual. The derivation of the cap follows directly from the
exponent relationships.
`cumulative(h) = 2²² · h / (h + 2²⁵)`. Because cost is strictly
increasing, the exchange argument says any delay yields less RES: the
cap needs no consensus counter. Realised stock is `ρ · 2²²`.

`128 = 2⁷` is the numerator of that identity, not an independent lucky
number. It is the miner-budget floor and the cap numerator in the same
list of powers of two.

## 5.4 Four checkable layers

| Layer | What is public |
|-------|----------------|
| Predicate | `(R, B, h)` — no hash, no hidden rate |
| Identity | Display form ↔ `<< 40` — shift forced by genesis cost |
| Ceiling | `(S / P0) · H = 2²²` — read off the integral |
| Exchange | Strictly increasing cost — delay yields less |

Earlier minters pay a lower unit cost. That is duration-proportional
payment, not a hole. This creates a duration-sensitive cost schedule:
earlier participation receives a lower protocol-defined destroy cost
because it commits capital earlier in the height timeline. Fairness
within the consensus rule remains deterministic public evaluation of
that curve. Height `h` is the order proof-of-work produces.

**The decision is public. The ceiling is derived. The shift is
forced.** See Appendix D for algebraic derivation:
[`RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md`](./RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md).
The Convergence curve defines deterministic protocol issuance cost.
It does not define market price, profitability, or future demand.
Differences in issuance cost across height do not constitute a protocol
guarantee of economic return. Chapter 6 states how mining supplies that
shared height — it pays ORU and does not mint RES.

---

# Chapter 6 — Proof-of-Work: The Shared Height, Paid in ORU

Chapter 5’s predicate contains `h`. If nodes disagree on height, the
same destroy is judged differently. This chapter states how that height
becomes shared, and what the finder is paid. The expanded form is
[`chapters/06-mining.md`](./chapters/06-mining.md).
Product path: [`chapters/07-mining-windows.md`](./chapters/07-mining-windows.md).

Mining **pays ORU**. Mining **does not mint RES**. Mining security and
reserve issuance are intentionally separate mechanisms.

```
PoW → ORU security budget
Convergence → RES issuance
```

## 6.1 The predicate needs a shared height

`destroy_cost(h)` is strictly increasing. Height cannot be a local
clock or a wallet guess. Proof-of-work produces a chain order.
Rewriting that order requires repeating the corresponding work. The
protocol does not claim that history cannot be rewritten under
sufficient hashpower. Confirming height is the block that includes the
transaction. Without that shared index, “same `(R, B, h)`” has no
public third component.

## 6.2 What is paid, and where

Genesis subsidy is 0. Every later block pays 128 ORU to the Witness v0
coinbase script. RES, if it appears, appears only in a body
Convergence transaction. Paying RES as a mining reward would inflate
the stock to buy security and recombine the two monetary functions.

```
OpuHash(header) = SHA3-256("RGRPOW01" || header_80)
valid           ⇔  OpuHash(header) ≤ target
```

OpuHash is used for **PoW validity**. Block **identity** remains
SHA256d of the same 80-byte header. The two hashes are not
interchangeable. Coinbase states this block’s height and pays ORU only.
Pool policy cannot allocate RES. Changing a payout address cannot
create RES. Finding a header does not legalise an underpaid mint.

## 6.3 Difficulty is not mint cost

Target spacing 600 s. ASERT half-life 43 200 s, clamp ±12.5%, parent
time/height only.

The ±12.5% bound is a consensus-level adjacent-target clamp: the target
selected for a block may move by at most 12.5% from its parent target.
It is deliberately conservative for abrupt hash-rate excursions,
particularly during the low-hashrate Genesis regime. This makes
difficulty transitions smoother, at the cost of potentially slower
block-time recovery after a large and persistent change in hash rate.
Under ordinary conditions, the ASERT step is typically much smaller
than the clamp, so the bound is normally inactive.

ASERT adjusts difficulty so block spacing tends toward the target.
Height remains a block-count index. ASERT does not bind `h` to
wall-clock time and does not enter the predicate.

The kernel models most-work finite-tip selection on `GetBlockProof`.
It does not prove `ActivateBestChain`.

**Height is shared. Payment is ORU. RES does not come from mining.**
Displayed mining status is not a source of validity. Public data can
be validated independently — Chapter 7.

---

# Chapter 7 — Local Verification: Independent Validation of Public Data

Chapter 6 said displayed mining status is not a source of validity.
This chapter states the substitute: public bytes recomputed locally.
The expanded form is
[`chapters/07-self-proving.md`](./chapters/07-self-proving.md).

The protocol does not claim that cheating is impossible. It claims that
unequal bytes fail the published checks.

```
public data → independent calculation → compare → accept / reject
```

Explorers, pools, wallets, and websites may **display** data.
Consensus validity is decided by full-node evaluation of the same
public bytes. Explorer displays; node decides; wallet constructs;
verifier recomputes. A displayed result is an observation. Consensus
validity is determined by independent verification.

## 7.1 Display is not consensus decision

If a displayed verdict is treated as consensus, the display becomes a
counterparty. The substitute is not a more authoritative website. It
is: take the public bytes, run the same function, compare.

The node `rigord` does this for every accepted block. A browser Zero
Trust page does it for selected fields. Paper evaluation of
`(R, B, h)` is the same standard: equal passes, unequal fails.

## 7.2 The commitment chain

Destroy `B`, mint `R`, height `h`, coinbase, Merkle root, OpuHash, and
block identity (SHA256d) are all in broadcast data. There is no
internal transaction path.

```
body tx → OP_CONVERGE B → Witness v1 sum R
    → R > 0 → R · (2²⁵ + h)² ≤ B << 40
        → coinbase (this block’s height; ORU only)
        → merkle root → header
        → OpuHash = SHA3-256("RGRPOW01" || header_80)
```

If any step fails to match, the block is invalid. An underpaid mint
that a pool already shows as credited is still invalid.

A fingerprint over public fields is observational. It does not accept
or reject blocks. A match is not a substitute for a full node.

**A conclusion may be displayed. Validity must be recomputed.**
Chapter 8 draws the boundary of formal proof.

---

# Chapter 8 — The Formal Boundary: The Model Is Not the Running Binary

Chapter 7 requires local recomputation. This chapter states the
current final formal conclusion: one public sentence; “yes” stops
inside the model. The expanded form is
[`chapters/08-formal-boundary.md`](./chapters/08-formal-boundary.md).

```
formal verification of the protocol model
        ≠
C++ equivalence
        ≠
running-binary proof
```

The verified object is the mathematical model. The running
implementation remains an engineering artifact subject to
implementation verification. See
[`RIGOR_FORMAL_VERIFICATION_DESIGN.md`](./RIGOR_FORMAL_VERIFICATION_DESIGN.md).

## 8.1 Three layers that are not equivalent

| Layer | What it is | What it is not |
|-------|------------|----------------|
| Running consensus | C++ `core/src/` realization of RFC-020 / RFC-018 | RFC / Coq rewriting running rules |
| Coq kernel model | Machine-checked properties of the model | A C++ refinement; `ConnectBlock` |
| Differential / extraction | Finite golden vectors; declared runtime path | Universal equivalence |

## 8.2 Scale is recorded in the inventory, not in this paper

Kernel: `formal_verification/rigor_kernel/`. Living inventory:
`release/FinalProofInventory.md`. Theorem counts, category splits,
maturity scores, and mutation-gate lists are dynamic engineering
state. They are not consensus parameters. They are not “N theorems
cover the running node.” This paper does not freeze those figures.

## 8.3 Side-by-side reading is not refinement

The names that receive “yes” in §8.5 share names with this paper.
Side-by-side reading is not a refinement proof. Hash in the model is
uninterpreted `H`. Ed25519 / SHA3 **security** is not proved.
Abstract `sig_ok` is not unforgeability. Those “yes” answers do not
skip Chapter 7.

## 8.4 Outside the boundary

Not proved: C++ ↔ Coq equivalence; `ConnectBlock` refinement;
extracted code computing the Coq function; compiler correctness;
k-deep / honest majority; ASERT envelope; primitive security;
network, hardware, wallet. Finite golden vectors catch regressions.
They do not rewrite agreement at locked points as universal
equivalence. Testing is not proof.

## 8.5 The final formal conclusion

The only public sentence:

> Inside the Coq consensus kernel model, critical consensus
> invariants have been machine-checked, with explicit extraction
> and runtime trust boundaries.

**Yes:** selected invariants inside this Coq model hold — kernel
soundness, ORU conservation, the RES ceiling, classifier partition,
chain induction. `Print Assumptions` status is recorded in the
inventory.

**Not yes:** the running C++ node equals the model.

**What is proved within the stated model is the model. What runs is
C++.** Product surfaces
should not read this chapter as “the chain has been proved” — Chapter
9.

---

# Chapter 9 — Product Surfaces: A Display Remains a Display

Chapter 8 drew the line between the model and the running binary.
This chapter states how wallets, explorers, pools, documentation, and
the website live with those rules. The expanded form is
[`chapters/09-product-surfaces.md`](./chapters/09-product-surfaces.md).

A surface may **display** a conclusion. It may not **be** that
conclusion. A green Chapter 8 is not “the chain has been proved.”
Product completeness is not consensus completeness. The website is
not authority. The explorer is not consensus. The pool is not an
issuer. Product completeness and consensus completeness are different
engineering objectives.

```
rules accepted or rejected in the node
        ↓
a surface displays public facts
        ≠
the surface becomes a source of validity
        ≠
a formal-verification page becomes an equivalence proof
```

## 9.1 Why surfaces and consensus coexist

Products exist so people can inspect balances, shares, height, and
explanation. Display is a condition of use. Display is not decision.
If products are treated as authority, validity moves from the node to
a screen.

Formal documentation is a sensitive surface. What it says can be true:
the model contains machine-checked invariants. Readers may then supply
the unwritten half-sentence — “so the chain has been proved.” Chapter
8 does not issue that half-sentence. Product copy should not issue it
on Chapter 8’s behalf. Shortening the allowed sentence rewrites the
honesty flags.

## 9.2 What a surface may do

| Surface | It may | It may not |
|---------|--------|------------|
| `rigord` | Accept or reject blocks | Be overridden by a website opinion |
| Wallet | Construct, spend, show balances | Be a validity oracle |
| Pool / mining | Show shares, height, ORU pay | Allocate RES; legalise an underpaid mint |
| Explorer | Retrieve; show fingerprints | Accept or reject blocks |
| Zero Trust | Recompute selected fields | Replace `ConnectBlock` |
| Docs / this paper | Bind the argument | Rewrite `core/src/` |
| Formal verification page | Publish the boundary | Write `equivalence_claimed = true` |
| Lab / experiment | Observe | Act as a refinement proof |
| Website | Entry and explanation | Consensus authority; “bug free” |

Use paths such as [`chapters/07-mining-windows.md`](./chapters/07-mining-windows.md)
are how to click, not another consensus. Living tools:
`/zero-trust-verification`, `/documentation/formal-verification`.
Noun list (not this chapter): [`chapters/09-glossary.md`](./chapters/09-glossary.md).

## 9.3 Three compressions that rewrite Chapter 8

1. Drop the word “model” from a Coq-green badge.
2. Write finite golden vectors as “C++ equals Coq.”
3. Write independent validation as “do not run a node.”

Product copy may repeat the one public sentence in Chapter 8. It may
not shorten it.

## 9.4 Documentation work is not a consensus change

Completing guides, checksums, placeholders, and chapter HTML does not
change the predicate, the 128 ORU subsidy, the classifier, or the
derived ceiling. A product “next step” can only mean clearer surfaces,
not a dated rewrite of the rules. No investment yield is promised. No
date is given for filling the derived RES ceiling.

**A display remains a display. The protocol defines the rules. The node realizes them.** Security
model and threat boundaries follow in Chapter 10.

---

# Chapter 10 — Security Model and Threat Boundaries

Chapters 1–9 stated the dual-asset rules, the verification act, and
the formal boundary. This chapter states which failures the protocol
is designed to detect, and which risks remain outside consensus.

The claim is architectural. The protocol defines **detect-and-reject
conditions**. It does not claim that attacks cannot occur, that
hashpower cannot rewrite recent history, or that market participation
will fund security.

```
full-node validation
        ↓
admit a legal state transition
        or
reject an illegal one
        ≠
the attack surface is empty
        ≠
external systems are in scope
```

## 10.1 Consensus attack boundaries

Full-node validation is designed to reject invalid state transitions.
The following cases are already named by live reject codes and
classification rules (RFC-020, Chapters 3–4). This section does not
add a rule.

| Case | Protocol response |
|------|-------------------|
| Invalid RES mint | Predicate `R > 0 → R · (2²⁵ + h)² ≤ B << 40` fails; the transaction is invalid |
| Missing `OP_CONVERGE` | `bad-txns-res-mint-no-burn`: a RES output without a visible destroy |
| Underpaid RES mint | `bad-txns-res-mint-underpaid`: destroy does not cover requested `R` |
| Illegal RES → ORU conversion | `illegal-res-to-oru-conversion`: reverse path, or spending a Convergence output |
| Invalid asset classification | Closed templates: non-listed script shapes are not accepted as new monetary outputs |
| Chain reorganisation | Most-work order; mint and coinbase maturity 100 as reorg protection |

A reorganisation can still occur. The protocol does not claim that
history cannot be rewritten under sufficient hashpower. Maturity 100
is a confirmation-depth convention already used for coinbase. It is
not a proof that a reorg of that depth is impossible.

An underpaid mint that a pool or explorer already displays as credited
remains invalid. Display does not admit a state transition. Chapter 7.

## 10.2 Economic attack boundaries

The protocol specifies a conversion mechanism. It does not guarantee
market participation.

| Pressure | What the protocol does | What it does not do |
|----------|------------------------|---------------------|
| ORU inflation pressure | Residual inflation level is `1 − ρ`; hardness is demand-led if destroy occurs | Guarantee that `ρ` is high, or that ORU has a terminal cap |
| Insufficient RES demand | Minting simply does not occur; `S_RES` stays below the derived ceiling | Create demand for RES |
| Low destroy participation | Realised stock is `ρ · 2²²`; the ceiling is an upper bound | Fill the ceiling |
| Weak fee market | RFC-018 destroys half of RES-transfer ORU fees; ORU subsidy remains 128 | Guarantee a fee market that replaces issuance |
| Miner incentive pressure | Coinbase pays 128 ORU per block after genesis | Guarantee the external purchasing power of that 128 ORU |

The security budget in protocol units has a floor. The security budget
in an external unit does not. Whether miners find the ORU payment
sufficient is a market outcome.

## 10.3 External dependency boundaries

The following are outside consensus, as already implied by Chapters 8
and 9:

- wallet software correctness and UX
- user key custody
- network connectivity and P2P honesty
- hardware reliability and side channels
- third-party services (pools, explorers, websites, indexers)

A full node may reject an illegal block. It cannot make a stolen key
unspendable, a disconnected peer honest, or an explorer’s display
authoritative. Formal verification of the Coq model does not cover
these surfaces, and it does not prove the running C++ binary.

## 10.4 Assumptions and limits

This is not a list of defects. It is the explicit model boundary.
The protocol does not prove honest majority, network behavior,
hardware availability, market adoption, exchange rate, wallet safety,
or compiler correctness.

## 10.5 What this chapter does not claim

- It does not claim that listed attacks cannot occur.
- It does not claim a security comparison with Bitcoin.
- It does not claim that 128 ORU is a sufficient real-world security
  budget.
- It does not claim that C++ is equivalent to the kernel model.
- It does not add a reject code, a maturity rule, or a hashpower
  assumption beyond what earlier chapters already stated.

**The protocol defines detection and rejection. It does not define
market demand.** Chapter 11 states what this architecture intentionally
does not provide.

---

# Chapter 11 — Design Trade-offs

Chapter 10 bounded consensus, economic, and external risks. This
chapter states what RIGOR intentionally does not provide, and what
those omissions are intended to buy.

The claim is architectural. A smaller surface is a design choice. It
is not a ranking of all public chains, and it is not a claim that
every application belongs on this ledger.

```
closed monetary templates
        +
UTXO-local validation
        +
one-way Convergence
        ≠
a general-purpose execution platform
```

## 11.1 What RIGOR does not optimise for

RIGOR intentionally does not optimise for:

- smart-contract execution
- arbitrary token issuance
- unlimited scripting
- account-based global state
- programmable asset logic

Those capabilities are useful in other systems. They are out of scope
here because they enlarge the consensus surface: more execution paths,
more ways for “what is this asset?” to depend on an indexer, a
contract store, or a later upgrade of a VM.

Chapter 3 already closed the template list. Production has no Script
VM. Identity is classification of `scriptPubKey`. Legitimacy is the
mint predicate and pool isolation. This chapter does not add a further
restriction; it records the trade-off.

## 11.2 Dual-asset complexity

The dual-asset design introduces complexity. That is a different
trade-off, not a claim that two assets are better than one. Benefits
within this model: a separate security budget, a separate scarcity
mechanism, and clearer monetary roles. Costs: two-asset accounting,
wallet UX complexity, and additional verification paths.

## 11.3 What the restrictions are intended to buy

Under this model, the intended returns are:

- a smaller consensus surface
- deterministic validation of public bytes
- easier independent verification (Chapter 7)
- simpler formal modelling (Chapter 8)

Deterministic validation means: the same `(R, B, h)` and the same
script shapes produce the same admit-or-reject. It does not mean
equal economic outcomes. Independent verification remains a full-node
act; a browser page is not `ConnectBlock`. Formal modelling remains a
model; it is not C++ equivalence.

## 11.4 These choices are not universal

A closed monetary surface is a poor fit for applications that need
general computation, custom issuance, or contract-mediated conversion.
Those applications belong on platforms designed for them.

RIGOR is an alternative architecture for two native monetary
functions coupled by a one-way destroy-to-mint. It is not presented
as strictly better than Bitcoin, and not as a replacement for
programmable chains.

## 11.5 What this chapter does not claim

- It does not claim that a smaller surface is always safer in
  operation.
- It does not claim that every omitted feature is a defect of other
  designs.
- It does not claim that independent verification is costless, or
  that users will run full nodes.
- It does not add a new template, opcode, or asset rule.

**The protocol buys a smaller, checkable monetary surface by omitting
a general execution platform.** Chapter 12 states how that surface is
maintained after deployment.

---

# Chapter 12 — Deployment and Evolution Policy

Chapter 11 recorded the omitted capabilities. This chapter states how
the deployed protocol is intended to be maintained: what is frozen,
and what may change as software.

The claim is policy, not a schedule. This chapter does not promise a
date, a hard fork, a feature, or a market outcome.

```
frozen consensus rules
        ≠
frozen wallets, explorers, and docs
        ≠
a promise of zero defects
```

## 12.1 Consensus freeze

RFC-018 and RFC-020 are the live issuance and fee-destroy rules.
They are frozen for the protocol described in this paper. The running
C++ node realizes those rules. It does not define them. If the
implementation diverges from the frozen protocol, the implementation
contains the defect. The defect does not redefine the protocol.

Updates to wallets, explorers, mining tools, and documentation are
not protocol upgrades. Chapter 9 already stated that completing a
guide or a page does not change the predicate, the 128 ORU subsidy,
the classifier, or the derived ceiling.

Documentation additions are not consensus changes. Protocol defines
the rules. Implementation realizes the protocol. Tests verify the
implementation.

## 12.2 Protocol change is not maintenance

This paper does not define an upgrade process.

A change to issuance rules, asset classification, transaction
validity, Proof-of-Work identity, or cryptographic validation is a
protocol change. It is not a software release of this protocol. It is
a different protocol.

Historical RFC numbers that are not live rules remain historical
(appendix). This paper does not edit RFC texts.

## 12.3 Software evolution boundary

May change without being a consensus change:

- wallet UI
- explorer presentation
- mining tools and GBT operational paths
- documentation and this paper’s wording, provided rules are not
  rewritten

Require strict review, because they *are* consensus if they change
admit-or-reject:

- consensus rules
- transaction validation
- asset-creation rules (mint locus, predicate, reverse ban,
  classification)

A display remaining a display (Chapter 9) still applies after a
wallet release.

## 12.4 Release philosophy

RIGOR does not promise:

- absence of bugs
- permanent operational stability
- market value, demand, or `ρ`

It does commit to keep public:

- the rules (RFC-018, RFC-020, and the running node)
- verification paths (full-node recomputation; selected-field tools)
- implementation boundaries (model ≠ C++; display ≠ validity)

Protocol design does not create market demand. When demand exists,
the published mechanisms translate it into security-related flows.
Whether demand arrives remains an external factor.

## 12.5 What this chapter does not claim

- It does not schedule a fork or name a next consensus RFC.
- It does not claim the running binary is bug-free.
- It does not claim the Coq model covers C++.
- It does not write a product release as a protocol upgrade.

**Rules public. Verification paths public. Implementation boundaries
public.** Lookup of frozen constants follows in the appendix.

---

# Appendix — Protocol Constants and Engineering Reference

Chapters 1–12 are the public argument. This appendix is lookup. It
does not add a claim. The expanded form is
[`chapters/10-appendix.md`](./chapters/10-appendix.md).

Realization lock files: `core/src/consensus/params.h`,
`core/src/consensus/rigor_consensus_lock.h`. The one public sentence
is Chapter 8 §8.5. This appendix does not repeat it.

## A. Frozen public parameters

| Symbol | Value |
|--------|-------|
| ORU subsidy | `R(0) = 0`; 128 ORU per block for `h ≥ 1`; no halving |
| Mint predicate | `R > 0 → R · (2²⁵ + h)² ≤ B << 40` |
| Display form | `destroy_cost(h) = 1024 · (2²⁵ + h)² / 2⁵⁰` (`2⁵⁰` = curve normalization denominator, not a consensus shift) |
| Genesis whole-coin cost | 1024 ORU / 1 RES |
| Pole / consensus scaling | `2²⁵` ; `40 = 50 − log₂(1024)` (`B << 40`; not the `destroy_cost` denominator) |
| Derived ceiling | `2²²` RES (`419430400000000` atoms) |
| Cumulative ceiling | `2²² · h / (h + 2²⁵)` |
| Mint locus | Body tx: `OP_CONVERGE` + same-tx Witness v1 RES |
| Mint maturity | 100 |
| RFC-018 | Half of RES-transfer ORU fees unclaimable |
| Reverse | RES → ORU forbidden |
| PoW | OpuHash = SHA3-256(`"RGRPOW01" \|\| header_80`) |
| Block identity | SHA256d of the same 80-byte header |
| ASERT | 600 s spacing; 43200 s half-life; ±12.5% clamp; does not bind `h` to wall-clock time |

Probes: `destroy_cost(0) = 102400000000`;
`destroy_cost(210000) = 103685749152`;
`destroy_cost(420000) = 104979520049`.

## B. Mint reject conditions

- `bad-txns-res-mint-underpaid` — `R · (2²⁵ + h)² > B << 40`
- `bad-txns-res-mint-no-burn` — RES output, no `OP_CONVERGE`
- `bad-txns-res-mint-cap-overshoot` — would exceed `419430400000000` atoms

## C. Scripts

Witness v0 / 20 bytes = ORU. Witness v1 / 32 bytes = RES (not
Taproot). `<HASH160> 0xbd` = unspendable Convergence. Coinbase states
this block’s height and pays ORU only. Spend signatures: Ed25519.

## D. Engineering index

`core/src/` is the running consensus. Kernel inventory:
`formal_verification/rigor_kernel/release/FinalProofInventory.md`.
Claim boundary: `docs/verification/FORMAL-VERIFICATION-STATUS.md`.
Reproduce from a full node against the published rules. Live issuance
docs: RFC-020, RFC-018. Noun list:
[`chapters/09-glossary.md`](./chapters/09-glossary.md).

## E. Terms

| Term | Official meaning |
|------|------------------|
| ORU | Operational Resource Unit. The operational asset produced by Proof-of-Work. Medium-of-exchange asset. Subsidy 128 after genesis |
| RES | Reserve End State. The reserve-oriented asset created through irreversible ORU destruction. Designed store-of-value asset within the protocol. Derived ceiling `2²²`; not mined |
| Convergence | Same-tx destroy that MAY mint RES under the predicate |
| `destroy_cost(h)` | Display cost. `2⁵⁰` is the curve normalization denominator. Consensus uses `B << 40`. Display rounding/floor do not enter consensus |
| OpuHash | Live proof-of-work validity hash (not block identity) |
| Fingerprint | Observational identity. Does not accept or reject blocks |

Lookup ends here. The claims are in Chapters 1–12.

Algebraic derivation of the mint identity is explanatory documentation
in Appendix D:
[`RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md`](./RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md).
That note does not introduce new consensus rules.

---

# Publication Boundary

This document describes the RIGOR protocol design and its stated
verification boundaries.

It does not claim:

- equivalence between the protocol model and production C++ code;
- compiler correctness;
- network-level guarantees;
- market adoption or economic outcomes.

Consensus validity remains determined by independently reproducible
node verification.

Standalone bibliography: [`REFERENCES.md`](./REFERENCES.md).

---

# References and Design Context

This section records intellectual and research context. It does not
revise Chapters 1–12, add a consensus rule, or state a market
forecast. RIGOR is a design exploration: an alternative architectural
approach to assigning monetary functions on a proof-of-work UTXO
ledger.

---

## Bitcoin foundation

RIGOR builds on several foundational ideas introduced by Bitcoin,
including proof-of-work ordering, decentralised block production, and
UTXO-based ownership transfer.

Bitcoin demonstrated that a permissionless network can establish a
shared transaction history without relying on a trusted intermediary.

RIGOR retains these principles while exploring a different native-asset
architecture.

Satoshi Nakamoto, *Bitcoin: A Peer-to-Peer Electronic Cash System*,
2008. <https://bitcoin.org/bitcoin.pdf>

---

## Security budget research background

Long-term miner incentive has been an active research question in
proof-of-work systems.

Bitcoin’s original design combines block subsidy and transaction fees
as miner compensation. As the subsidy declines over time, researchers
and practitioners have discussed whether future fee markets can provide
sufficient security incentives.

That discussion remains an open question rather than a settled
economic conclusion. This paper does not treat it as a prediction that
any particular fee market will or will not suffice.

RIGOR approaches the same research question by a different design
exploration: it separates the miner-payment function from the
store-of-value function at the consensus asset layer. That is an
alternative architectural approach, not a claim that the Bitcoin design
has been disproved.

Literature on this open question includes, among other academic and
industry discussions of long-term miner incentives, Miles Carlsten,
Harry Kalodner, S. Matthew Weinberg, and Arvind Narayanan, *On the
Instability of Bitcoin Without the Block Reward*, ACM CCS, 2016.

---

## Crypto-economic background

RIGOR’s discussion of medium of exchange (MoE), store of value (SoV),
and security incentives is informed by previous crypto-economic
research, including:

Nervos RFC-0015, *Crypto-Economics of the Nervos Common Knowledge
Base*.

That research examined the challenge of assigning multiple economic
roles to a single native asset. RIGOR shares the observation that
different participants may have different economic objectives and time
horizons. RIGOR follows a different architectural path.

Nervos CKB focuses on a state-capacity-oriented model in which the
native asset is connected with preservation of on-chain resources.
RIGOR does not use state rent, state capacity, or a smart-contract
asset platform.

Instead, RIGOR separates these roles directly at the consensus asset
layer, as already specified in Chapters 1–2 and RFC-020:

- ORU (Operational Resource Unit) is the operational asset produced by Proof-of-Work, and the circulation and miner-payment asset.
- RES (Reserve End State) is the reserve-oriented asset created through irreversible ORU destruction.
- ORU destruction is the consensus-defined path that may create RES.

The protocol defines the transformation rules. Market adoption and
asset valuation remain external outcomes.

---

## Scope statement

These references describe intellectual and research context only.

They do not imply protocol inheritance, implementation dependency,
consensus compatibility, or economic-outcome guarantees.

RIGOR should be evaluated as an independent protocol design.
