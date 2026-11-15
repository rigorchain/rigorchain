# SPEC-005: RIGOR Consensus Invariants

**Document status:** Production specification  
**Track:** Formal Specification Program  
**Status:** ESTABLISHED  
**Baseline:** RFC-020 shipping issuance (continuous `destroy_cost(h)`, flat 128 ORU)  
**Parents:** [SPEC-000](./SPEC-000-CONSENSUS-SPECIFICATION.md), [SPEC-001](./SPEC-001-STATE-MODEL.md), [SPEC-002](./SPEC-002-ECONOMIC-MODEL.md), [SPEC-003](./SPEC-003-CONSENSUS-RULES.md), [SPEC-004](./SPEC-004-TRUST-ANCHORS.md)  
**Normative sources:** `RFC-000`, `RFC-008`, `RFC-020`  
**Freeze authority:** `MAINNET_PROTOCOL_FREEZE_DECLARATION.md` / `historical verification record`

---

## Document Role

This document is the **authoritative invariant specification** for RIGOR. It defines
properties that MUST remain true across all valid states and all valid transitions.

This document is specification-only:

- No code changes
- No consensus changes
- No proofs
- **Equivalence Claimed:** false

---

## 1. Purpose

An **invariant** is a property that MUST hold in:

- `State(h)` at every height `h` on the active chain, and
- `State(h+1)` produced by every valid transition from `State(h)`

Formally, for invariant `CI-*`:

```
∀ h ≥ 0:  CI holds at State(h)
∀ valid Block(h+1):  CI holds at State(h) ⟹ CI holds at State(h+1)
```

**Violation of any invariant implies consensus failure.** A system that permits
a valid transition violating any `CI-*` defined here is not conformant to the
RIGOR specification, regardless of runtime behavior.

Invariants are **falsifiable**: each MUST admit a concrete counterexample
(divergent replay, invalid supply, double spend, etc.).

---

## 2. Invariant Classification

| Category | Prefix | Scope |
|----------|--------|-------|
| **CI-CONSENSUS** | CI-001 … CI-003 | Chain selection, validation determinism |
| **CI-STATE** | CI-010 … CI-014 | UTXO, transition, replay, reorg |
| **CI-ECONOMIC** | CI-020 … CI-024 | ORU and RES supply laws |
| **CI-MINT** | CI-030 … CI-033 | Body-tx mint arithmetic and authorization |
| **CI-FREEZE** | CI-040 … CI-042 | Immutable parameters and protocol freeze |

Each invariant below lists: **statement**, **failure condition**, and **related
rules** from [SPEC-003](./SPEC-003-CONSENSUS-RULES.md) where applicable.

---

## 3. Consensus Invariants (CI-CONSENSUS)

### CI-001: Chain Continuity

**Category:** CI-CONSENSUS

**Statement:** Every non-genesis block at height `h ≥ 1` references exactly one
valid predecessor: its parent hash equals the block hash at height `h − 1` on the
active chain, and heights are consecutive from genesis.

**Failure condition:** An accepted block is **disconnected** — parent hash does
not match the active tip, height is not `h + 1`, or the ancestor chain is broken.

**Related rules:** CR-002, CR-010, CR-011

---

### CI-002: Single Active Chain

**Category:** CI-CONSENSUS

**Statement:** At any point in validation, a node has exactly one **authoritative**
active chain tip. Fork choice selects a unique tip by greatest cumulative chainwork
with deterministic tie-break.

**Failure condition:** Two simultaneous authoritative active tips at the same
logical validation point; or equal-work fork resolved non-deterministically.

**Related rules:** CR-011, CR-061

---

### CI-003: Deterministic Validation

**Category:** CI-CONSENSUS

**Statement:** All honest nodes evaluating the same `(State(h), Block(h+1))`
produce the same accept-or-reject verdict.

**Failure condition:** **Node disagreement** — two correct validators differ on
whether `Block(h+1)` is valid given identical `State(h)`.

**Related rules:** CR-050, CR-003

---

## 4. State Invariants (CI-STATE)

### CI-010: UTXO Consistency

**Category:** CI-STATE

**Statement:** Every spent output in a valid block previously existed in the UTXO
set at `State(h)` before the spend is applied.

**Failure condition:** A transaction spends an outpoint `(txid, index)` not present
in `UTXO` at `h`.

**Related rules:** CR-020

---

### CI-011: No Double Spend

**Category:** CI-STATE

**Statement:** A UTXO entry MAY be consumed at most once across the active chain.
Within a single block, no outpoint appears in more than one input.

**Failure condition:** The same outpoint is spent twice — within one block or
across the committed chain history.

**Related rules:** CR-021

---

### CI-012: Deterministic State Transition

**Category:** CI-STATE

**Statement:**

```
Transition(State(h), Block(h+1))
```

produces **exactly one** successor state `State(h+1)` when the block is valid.

**Failure condition:** Two correct validators accept the same block but produce
non-identical `State(h+1)`.

**Related rules:** CR-050, CR-060, CR-051

---

### CI-013: Replay Equivalence

**Category:** CI-STATE

**Statement:** Replaying blocks `B(1..h)` from genesis (or an agreed checkpoint)
always yields identical `State(h)` on all correct validators.

**Failure condition:** Two replays of the same block sequence produce divergent
`State(h)`.

**Related rules:** CR-062

---

### CI-014: Reorg Consistency

**Category:** CI-STATE

**Statement:** Switching to the winning chain by reorganization yields the same
`State(h')` as sequential application of that chain from genesis.

**Failure condition:** Post-reorg state differs from direct replay of the winning
chain to the same height.

**Related rules:** CR-063

---

## 5. Economic Invariants (CI-ECONOMIC)

Economic quantities are defined in [SPEC-002](./SPEC-002-ECONOMIC-MODEL.md).

### CI-020: ORU Supply Non-Negativity

**Category:** CI-ECONOMIC

**Statement:**

```
∀ h:  S_ORU(h) ≥ 0
```

**Failure condition:** Cumulative ORU supply becomes negative at any height.

**Related rules:** CR-033

---

### CI-021: ORU Recurrence Preservation

**Category:** CI-ECONOMIC

**Statement:** For every valid transition to height `h+1`:

```
S_ORU(h+1) = S_ORU(h) + R(h+1) − C(h+1)
```

where `R(h+1)` is the frozen block subsidy and `C(h+1)` is the converge sum
(ORU committed by convergence transactions).

**Failure condition:** Supply counters violate the recurrence after a block
accepted as valid.

**Related rules:** CR-030, CR-031, CR-032, CR-052

---

### CI-022: RES Recurrence Preservation

**Category:** CI-ECONOMIC

**Statement:** For every valid transition to height `h+1`:

```
S_RES(h+1) = S_RES(h) + body_mint_atoms(h+1)
```

where `S_RES(0) = 0` and `body_mint_atoms(h+1)` is the sum of Witness v1 RES
outputs from body transactions that pass `CheckResMintArithmetic`.

**Failure condition:** RES cumulative supply does not satisfy the recurrence.

**Related rules:** CR-044, CR-052

---

### CI-023: RES Supply Cap

**Category:** CI-ECONOMIC

**Statement:**

```
∀ h:  S_RES(h) ≤ 419430400000000
```

**Failure condition:** A single transaction is accepted with `R` above
`419430400000000` RES atoms. `CheckResMintArithmetic` rejects that `R`.
Validation does not keep a cumulative RES counter and does not reject a
mint because a running total has already reached the cap. The cap is the
closed-form upper bound from RFC-020.

**Related rules:** CR-045

---

### CI-024: RES Mint Predicate

**Category:** CI-ECONOMIC

**Statement:** Every RES-creating output in a valid block belongs to a body
transaction that satisfies `R · (2²⁵+h)² ≤ B << 40`
(`CheckResMintArithmetic`). A block MAY contain zero or more such transactions.

**Failure condition:** RES is created without the mint predicate, or a mint
output fails `CheckResMintArithmetic`.

**Related rules:** CR-040, CR-043, CR-044

---

## 6. Mint Invariants (CI-MINT)

Mint semantics are specified in [SPEC-003](./SPEC-003-CONSENSUS-RULES.md) §7.

### CI-030: Mint Determinism

**Category:** CI-MINT

**Statement:** Identical mint inputs — `State(h)`, `Block(h+1)`, body
`(R, B, h)` — always produce the same `CheckResMintArithmetic` accept/reject
and the same `body_mint_atoms`.

**Failure condition:** Same inputs yield different mint verdicts on two correct
validators.

**Related rules:** CR-041

---

### CI-031: Mint Arithmetic Soundness

**Category:** CI-MINT

**Statement:** Mint comparison is integer-only. Products in
`CheckResMintArithmetic` use a 128-bit intermediate and MUST NOT overflow
into a wraparound accept. Floating-point is forbidden.

**Failure condition:** A mint is accepted or rejected because of overflow,
wraparound, or non-integer arithmetic.

**Related rules:** CR-040, CR-044

---

### CI-032: Price Independent of PoW

**Category:** CI-MINT

**Statement:** `destroy_cost(h)` is a pure height function. It MUST NOT be derived from
PoW difficulty, header `nBits`, ASERT state, or any variable consensus quantity.

**Failure condition:** A mint predicate uses PoW difficulty as the destroy cost.

**Related rules:** CR-042, CR-046

---

### CI-033: Valid Mint Requires CheckResMintArithmetic

**Category:** CI-MINT

**Statement:** A successful RES mint at height `h` implies
`CheckResMintArithmetic(R, B, h)` accepted for that body transaction.

**Failure condition:** RES mint exists in a block without
`CheckResMintArithmetic`.

**Related rules:** CR-040, CR-043, CR-044

---

## 7. Freeze Invariants (CI-FREEZE)

### CI-040: Frozen Parameter Preservation

**Category:** CI-FREEZE

**Statement:** All validation predicates use only the immutable constitutional
constant set frozen at mainnet genesis. Parameters including `RES_COIN`,
`RES_DESTROY_POLE`, `RES_DESTROY_SHIFT`, `RES_MAX_SUPPLY = 419430400000000`,
flat 128 ORU, and genesis-only activation MUST NOT change without Level-5
governance.

**Failure condition:** A validator accepts a block using a non-frozen parameter
value.

**Related rules:** CR-012

---

### CI-041: Monetary Policy Preservation

**Category:** CI-FREEZE

**Statement:** ORU subsidy schedule and RES issuance rules (flat 128 ORU,
pole mint predicate, derived cap `2²²`) MUST remain identical to the frozen
RFC-020 mainnet economics for all valid blocks.

**Failure condition:** Acceptance of blocks under a different subsidy or mint
predicate than the frozen RFC-020 set.

**Related rules:** CR-030, CR-044, CR-045, CR-012

---

### CI-042: Protocol Freeze Preservation

**Category:** CI-FREEZE

**Statement:** Consensus semantics remain identical to the protocol frozen under
`MAINNET_PROTOCOL_FREEZE_DECLARATION.md` / `historical verification record`. No
consensus rule drift, silent amendment, or implementation-defined behavior may
replace specified predicates.

**Failure condition:** Valid blocks accepted under semantics outside the frozen
rule bundle (RFC-020 mainnet).

**Related rules:** CR-013, CR-012; see also CO-7 in SPEC-000

---

## 8. Invariant Dependency Graph

Dependencies express logical prerequisite relationships. An invariant MAY depend on
others whose truth is required for it to hold or be meaningfully stated.

**Economic chain:**

- **CI-020** (ORU non-negativity) **depends on** **CI-021** (ORU recurrence).
  Non-negativity is meaningful only when subsidy and convergence commitments are correctly applied.

- **CI-023** (RES cap) **depends on** **CI-022** (RES recurrence).
  Cap enforcement presumes correct per-block mint accounting.

**Mint-arithmetic chain:**

- **CI-024** (mint predicate) **depends on** **CI-031** (arithmetic soundness).
  The inequality is meaningful only when products do not wrap.

- **CI-033** (valid mint requires `CheckResMintArithmetic`) **depends on**
  **CI-030** (mint determinism) **and** **CI-031** (arithmetic soundness).

**Freeze chain:**

- **CI-042** (protocol freeze preservation) **depends on** **CI-040** (frozen
  parameters) **and** **CI-041** (monetary policy preservation).

- **CI-032** (price independent of PoW) **depends on** **CI-040** (frozen
  parameters), specifically `destroy_cost(h)`.

**State–consensus chain:**

- **CI-012** (deterministic transition) **depends on** **CI-003** (deterministic
  validation).

- **CI-013** (replay equivalence) **depends on** **CI-012** (deterministic
  transition).

- **CI-014** (reorg consistency) **depends on** **CI-013** (replay equivalence).

**UTXO chain:**

- **CI-011** (no double spend) **depends on** **CI-010** (UTXO consistency).

**Independent roots (no CI-* dependencies listed above):**

- CI-001, CI-002, CI-003 (consensus layer roots)
- CI-010 (UTXO existence root)

---

## 9. Relationship to Formal Verification

```
Consensus Rules (SPEC-003)
        │  defines: CR-* validation predicates
        ▼
State Model (SPEC-001)
        │  defines: State(h), transition shape
        ▼
Consensus Invariants (this document)
        │  defines: CI-* properties that MUST hold
        ▼
Coq Proof Obligations
        │  proves: theorems implying CI-* under stated trust anchors
        ▼
Runtime Witness Alignment (FVR, Phase 5)
           observes: evidence only; equivalence_claimed = false
```

**Every future theorem MUST ultimately support one or more CI-*.** Theorems that
do not imply a falsifiable invariant are out of scope for the consensus proof
program.

Trust anchors used in proofs MUST be cited from [SPEC-004](./SPEC-004-TRUST-ANCHORS.md).
Open audit items (A-1 … A-5) block equivalence claims that depend on them.

---

## 10. Relationship to Runtime Witnesses

The Formal-Friendly Runtime Refactor track (FVR-A through FVR-N) produces runtime
supply mirrors, witness chains, invariant checkers, catalog exports, and Phase-5
alignment artifacts.

**Binding statement:**

| Property | Rule |
|----------|------|
| FVR defines invariants | **NO** — CI-* are defined only in this specification layer |
| FVR observes invariant satisfaction | **YES** — diagnostic reports may reference CI-* or parallel stable ids |
| FVR failure causes block rejection | **FORBIDDEN** on mainnet |
| FVR exports are authoritative | **NO** |

Runtime witnesses produce **evidence only**. A passing FVR invariant report does
not prove CI-*; a failing report does not redefine CI-*.

---

## 11. Freeze Compatibility

This document is **specification only**. It documents properties the frozen
protocol MUST satisfy; it does not modify consensus, monetary policy, or the
protocol freeze declaration.

| Guarantee | Status |
|-----------|--------|
| No consensus modifications | **Confirmed** |
| No monetary-policy modifications | **Confirmed** |
| No protocol-freeze modifications | **Confirmed** |

Nothing in this document supersedes `MAINNET_PROTOCOL_FREEZE_DECLARATION.md` or
`historical verification record`.

---

## 12. Mapping: SI-* to CI-*

[SPEC-001](./SPEC-001-STATE-MODEL.md) §8 listed placeholder **SI-*** identifiers.
This document supersedes those placeholders with authoritative **CI-*** invariants:

| SI (placeholder) | CI (authoritative) |
|------------------|-------------------|
| SI-001 | CI-001 |
| SI-002 | CI-010, CI-011 |
| SI-003 | CI-020 |
| SI-004 | CI-023 |
| SI-005 | CI-040 |
| SI-006 | CI-021 (convergence component) |
| SI-007 | CI-024 |
| SI-008 | CI-002 (chainwork aspect) |
| SI-009 | CI-033 |
| SI-010 | CI-032 (RATE / height binding) |
| SI-011 | CR-023 / script-as-compliance (transaction rule) |
| SI-012 | Identity domain (txid; transaction rule) |
| SI-013 | CI-012, CI-003 |

---

## 13. Task Output Status

| Field | Status |
|-------|--------|
| Consensus Invariants Specification | **ESTABLISHED** |
| Consensus Invariants | **ESTABLISHED** |
| State Invariants | **ESTABLISHED** |
| Economic Invariants | **ESTABLISHED** |
| Mint Invariants | **ESTABLISHED** |
| Freeze Invariants | **ESTABLISHED** |
| Consensus Impact | **NONE** |
| Equivalence Claimed | **false** |

---

## 14. Revision History

| Version | Date | Change |
|---------|------|--------|
| 0.2 | 2026-09-12 | RFC-020: CI-MINT pole predicate; `body_mint_atoms` cap |
| 0.1 | 2026-06-05 | `historical verification record` — initial consensus invariants specification |
