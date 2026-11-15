# RIGOR Specification Index

**Track:** Formal Specification Program  
**Document status:** Production specification  
**Purpose:** Canonical index of the specification layer. All future formal models and
proofs MUST reference documents listed here.

> **Parameter boundary.** Live issuance is RFC-020 (`docs/protocol/RFC-020.md`).
> RFC-014, RFC-015, and RFC-019 are not the issuance parameters of this node.

---

## Authority

The specification layer defines what RIGOR **SHOULD** do. It does not define what
any implementation currently does. Implementation is evidence for alignment, not a
source of normative semantics.

**Hierarchy:**

```
Specification  →  Formal Model  →  Proof  →  Runtime Alignment Evidence
```

**Specification supersedes implementation** (see `SPEC-000` §7 and
[`../formal/SSOT_HIERARCHY.md`](../formal/SSOT_HIERARCHY.md)).

**Freeze boundary:** Nothing in this index or its documents supersedes
`MAINNET_PROTOCOL_FREEZE_DECLARATION.md`.

---

## Published Documents

| ID | Title | Status | Description |
|----|-------|--------|-------------|
| **SPEC-000** | [Consensus Specification](./SPEC-000-CONSENSUS-SPECIFICATION.md) | **ESTABLISHED** (skeleton) | Scope, system model, consensus objectives, assumptions, trust anchors, non-goals, formal-verification relationship, freeze compatibility |
| **SPEC-001** | [State Model](./SPEC-001-STATE-MODEL.md) | **ESTABLISHED** | State categories, minimal consensus state, runtime/observational/external state, transition model, SI placeholders, FVR boundary, freeze compatibility |
| **SPEC-002** | [Economic Model (ORU/RES)](./SPEC-002-ECONOMIC-MODEL.md) | **ESTABLISHED** | ORU subsidy/convergence recurrence, RES mint/cap recurrence, falsifiable economic invariants, diagnostic cross-checks (non-authoritative), freeze compatibility |
| **SPEC-003** | [Consensus Rules](./SPEC-003-CONSENSUS-RULES.md) | **ESTABLISHED** | Seven-layer validation hierarchy, CR-001..CR-063 predicates, failure model, FVR boundary, freeze compatibility |
| **SPEC-004** | [Trust Anchors](./SPEC-004-TRUST-ANCHORS.md) | **ESTABLISHED** | Hashing/PoW/clock/network/storage/draw anchors, A-1..A-5 open audit registry, failure conditions, freeze compatibility |
| **SPEC-005** | [Consensus Invariants](./SPEC-005-CONSENSUS-INVARIANTS.md) | **ESTABLISHED** | CI-CONSENSUS/STATE/ECONOMIC/DRAW/FREEZE invariants, dependency graph, FVR boundary, SI→CI mapping, freeze compatibility |
| **SPEC-006** | [Runtime / Formal Mapping](./SPEC-006-RUNTIME-FORMAL-MAPPING.md) | **ESTABLISHED** | Spec→formal→Coq→witness mappings, MP principles, CI/CO/TA/FVR tables, equivalence boundary, verification hierarchy |
| **SPEC-007** | [Equivalence Boundaries](./SPEC-007-EQUIVALENCE-BOUNDARIES.md) | **ESTABLISHED** | CES/ECS layers, FVR-I..N closure boundaries, draw impossibility, PD-4F debt, future scoped equivalence conditions |
| **SPEC-008** | [Proof-of-Work (SHA3-256)](./SPEC-008-PROOF-OF-WORK.md) | **CURRENT PRODUCTION** | OpuHash is FIPS SHA3-256(`"RGRPOW01" \|\| header_80`). SHA256d is the block id, merkle root, and txid. |

---

## Proof of work

SPEC-008 records the live Proof-of-Work digest. Production `core/src/pow.cpp`
implements it through `GetRigorPowHash`. `GetOpusHash` in `pow_opus.cpp` is
not that function.

---

## Planned Documents (Placeholders)

All planned specification-layer documents through SPEC-007 are **ESTABLISHED**.
SPEC-008 is **implemented** in production `CheckProofOfWork`.

| ID | Title | Status | Scope (planned) |
|----|-------|--------|-----------------|
| *(none)* | — | — | Specification layer PHASE-1 skeleton complete |

---

## Related Normative Sources (External to SPEC Layer)

These ratified documents inform the specification layer until superseded by an
ESTABLISHED SPEC entry:

| Document | Role |
|----------|------|
| [`docs/protocol/RFC-020.md`](../protocol/RFC-020.md) | Deterministic Economics for Human and Machine Verification. Live issuance |
| [`docs/protocol/RFC-018.md`](../protocol/RFC-018.md) | RES-transfer fee burn |
| `docs/whitepaper/RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md` | Public mathematics |
| [`docs/protocol/MAINNET_PROTOCOL_FREEZE_DECLARATION.md`](../protocol/MAINNET_PROTOCOL_FREEZE_DECLARATION.md) | Mainnet freeze statement |
| [`docs/developer/Architecture.md`](../developer/Architecture.md) | Engineering architecture (non-normative for consensus semantics) |

---

## Formal Program Status

| Field | Status |
|-------|--------|
| Specification Layer | **INITIALIZED** |
| Consensus Specification | **ESTABLISHED** (`SPEC-000`) |
| State Specification | **ESTABLISHED** (`SPEC-001`) |
| Economic Specification | **ESTABLISHED** (`SPEC-002`) |
| Consensus Rules Specification | **ESTABLISHED** (`SPEC-003`) |
| Trust Anchor Specification | **ESTABLISHED** (`SPEC-004`) |
| Consensus Invariants Specification | **ESTABLISHED** (`SPEC-005`) |
| Runtime / Formal Mapping Specification | **ESTABLISHED** (`SPEC-006`) |
| Equivalence Boundaries Specification | **ESTABLISHED** (`SPEC-007`) |
| Proof-of-Work Specification | **PROPOSED** (`SPEC-008`) — Level-5, pre-mainnet only |
| Consensus Impact | **NONE** (SPEC-008 carries consensus impact **only on ratification**) |
| Equivalence Claimed | **false** |
| Coq ≡ Runtime | **NOT ESTABLISHED** |

---

## Revision History

| Version | Date | Change |
|---------|------|--------|
| 0.1 | 2026-06-05 | Index created; SPEC-000 established |
| 0.2 | 2026-06-05 | SPEC-001 state model established |
| 0.3 | 2026-06-05 | SPEC-002 economic model established |
| 0.4 | 2026-06-05 | SPEC-003 consensus rules established |
| 0.5 | 2026-06-05 | SPEC-004 trust anchors established |
| 0.6 | 2026-06-05 | SPEC-005 consensus invariants established |
| 0.7 | 2026-06-05 | SPEC-006 runtime/formal mapping established |
| 0.8 | 2026-06-05 | SPEC-007 equivalence boundaries established; specification layer complete |
| 0.9 | 2026-08-30 | SPEC-008 proof-of-work added (SHA3-256 / `RGRPOW01`) |
