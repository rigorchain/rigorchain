# RIGOR Protocol Identity

**Document status:** Public protocol narrative (not consensus SSOT)  
**Date:** 2026-09-04  

This paper states protocol purpose, identity, and security assumptions.
Code facts: [`../developer/CODE_SSOT_PROTOCOL_FACTS.md`](../developer/CODE_SSOT_PROTOCOL_FACTS.md).

```
equivalence_claimed = false
documentation_consensus_authority = false
refinement_proved = false
```

This is one of **three public core whitepapers**. It answers: **What is RIGOR?**

```
RIGOR is a dual-asset UTXO protocol built on script-derived asset
classification, stateless monetary architecture, and machine-checked
invariants of the protocol model.
```

Machine-checked consensus invariants, when the phrase is used at all,
are properties of the **protocol model**. The public sentence is
Chapter 8 of the protocol whitepaper. It does not mean C++ equivalence,
production-binary proof, or complete system verification.

```
equivalence_claimed = false
```

The other two public cores:

| Paper | Question |
|-------|----------|
| [`RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md`](./RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md) | How does the mathematics run? |
| [`RIGOR_FORMAL_VERIFICATION_DESIGN.md`](./RIGOR_FORMAL_VERIFICATION_DESIGN.md) | What is the model, and where does proof stop? |

Explorer is an independent product:
[`../explorer/EXPLORER_ARCHITECTURE.md`](../explorer/EXPLORER_ARCHITECTURE.md).

---

## Protocol identity

```
RIGOR =
    Dual Native Assets
  +
    UTXO Monetary Model
  +
    Deterministic Issuance
  +
    Deflationary Mechanisms
  +
    Same-tx destroy-to-mint at destroy_cost(h)
  +
    OpuHash Proof-of-Work
  +
    Cryptographic Ownership
```

---

## Dual native assets

| Asset | Role |
|-------|------|
| **ORU** | Base transaction asset, fees, mining subsidy, Convergence destroy cost |
| **RES** | Reserve asset; same-tx body mint at `destroy_cost(h)` — not a PoW reward |

There are exactly two native assets. There is no explorer asset, no
indexed token, and no third consensus money.

---

## No global monetary state

Monetary architecture is **stateless** in the account-ledger sense:

- no global asset table
- no account-balance consensus object
- no explorer database as ledger of record
- fee destroy is not a UTXO and not a wallet balance

Consensus state is the UTXO set plus validation rules. Rebuildable
indexes do not hold monetary truth.

```
Consensus State ≠ UTXO Set + Explorer Database + Global Asset Table
```

---

## Script-derived asset classification

```
CTxOut = (value, scriptPubKey)

AssetClass = f(scriptPubKey)
```

Script is the **compliance / classification layer**. Classification is
not issuance authority. Script alone does not create ORU or RES.

There is no `(asset, value, script)` storage field.

---

## UTXO architecture

Spendable ownership is UTXO transitions: consume old outputs, create new
ones. Ordinary transfers cannot mint. Authorized mint paths are explicit
consensus exceptions (ORU coinbase subsidy; RES same-tx body mint at
`destroy_cost(h)`). Coinbase `OP_RES_MINT` is not the RFC-020 path.

**Consensus defines reality.** Explorer displays consensus-derived data.

---

## Proof-of-Work

Blocks enter history only if Proof-of-Work is valid. The consensus algorithm
name is **OpuHash**. OpuHash is SHA3-256 over the domain-tagged header:

```
OpuHash(header) = SHA3-256("RGRPOW01" || header_80)
valid           ⇔  OpuHash(header) ≤ target
```

`RGRPOW01` is the 8-byte domain tag. The primitive is FIPS 202 SHA3-256
(Keccak-f[1600], domain suffix `0x06`). Block identity remains
`SHA256d(header_80)`. Merkle and txid remain SHA256d. σ is not a live
mint input.

Difficulty follows ASERT (mainnet half-life 43200 seconds). PoW orders
blocks and issues ORU subsidy. PoW does not mint RES.

Source: `rigor_kernel::GetRigorPowHash` in `core/src/rigor_kernel/pow_digest.h`;
`CheckProofOfWork` in `core/src/pow.cpp`.

`GetOpusHash` in `pow_opus.cpp` is not OpuHash. `CheckProofOfWork` does not call it.

---

## Convergence

Live Convergence is same-tx destroy-to-mint: `OP_CONVERGE` /
`RIGOR_MARKER_CONVERGE` (`0xbd`, historically `OP_CONVERGE`) destroys
ORU amount `B`. The **same body transaction** MAY mint Witness v1 RES
summing to `R` when

```
R > 0  →  R · (2²⁵ + h)² ≤ B << 40
```

`destroy_cost(h)` is a protocol destroy cost, not a market quote. There is no Script VM.
Mint-output maturity 100 is reorg protection.

Equations live in the mathematics paper. Invariants and proof boundaries
live in the verification paper.

---

## Observer architecture

Explorer and indexer are observation systems. Their databases are
rebuildable. Deleting them does not change the UTXO set, ORU, RES, or
blocks.

```
database_reset ≠ monetary_reset
```

---

## Security model

| Layer | Role |
|-------|------|
| UTXO conservation | Ordinary transfers cannot create ORU or RES |
| Script validation | Closed monetary script families; classification ≠ issuance |
| Proof-of-Work | OpuHash = SHA3-256 (`RGRPOW01`) with ASERT difficulty |
| Ed25519 | Cryptographic ownership of spendable outputs |
| Formal protocol properties | Model invariants — not C++ refinement |

---

## Final identity

```
RIGOR Protocol Reality
=
Consensus Validation
+ UTXO Ledger
+ Script Rules
+ Monetary Rules
+ PoW Rules
```

```
equivalence_claimed = false
```

