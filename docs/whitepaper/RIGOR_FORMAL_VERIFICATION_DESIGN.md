# RIGOR Formal Verification Design

**Document status:** Design specification (not the live claim SSOT)  
**Date:** 2026-09-14  
**Aligned to:** RFC-020 issuance and the C++ node. A model inventory label is not a proof of that node.

This paper states security assumptions, verification **targets**, and
implementation limitations. Formal results are properties of the Coq
consensus-kernel **model**, not a C++ equivalence proof.

Live claim status:
[`../verification/FORMAL-VERIFICATION-STATUS.md`](../verification/FORMAL-VERIFICATION-STATUS.md).
Classified theorems:
[`../../formal_verification/rigor_kernel/release/FinalProofInventory.md`](../../formal_verification/rigor_kernel/release/FinalProofInventory.md).
Public narrative:
[`chapters/08-formal-boundary.md`](./chapters/08-formal-boundary.md).

```
REFINEMENT_PROVED=false
CPP_KERNELINPUT_EQUIVALENCE=false
LEVEL_5_CLAIMED=false
CPP_VERIFIED=false
equivalence_claimed = false
hash_kernel_shared_binary_with_cpp = false
refinement_proved = false
mathematical_documentation_is_code_proof = false
```

`formal_verification_means_model_property_proof` (wording): in this
project, “formal verification” **means** machine-checked properties of
the protocol **model**. It does **not** mean the deployed system is
proved, and it is **not** `CPP_VERIFIED`.

This paper is the **design** document for “what the model is for and
where proof stops.” It is **not** the unique public document for “Why
is RIGOR trustworthy?” That sentence, if used at all, is the ALLOWED
claim in the source of truth:

> Formally verified critical consensus invariants inside a Coq
> consensus kernel model, with explicit extraction and runtime trust
> boundaries.

It is independent of the identity paper and of the mathematical design
paper. It does not replace either.

```
RIGOR_PROTOCOL_IDENTITY.md
        |
        |  What is RIGOR?
        v
RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md
        |
        |  How does the mathematics run?
        v
RIGOR_FORMAL_VERIFICATION_DESIGN.md
        |
        |  What is the model, and where does proof stop?
        v
FORMAL_STATUS_SOURCE_OF_TRUTH_v1.md
+ FinalProofInventory.md
+ chapters/08-formal-boundary.md
        |
        |  What may be said today (4.9)?
```

---

## Opening statement

```
Consensus defines reality.
Explorer displays reality.

Formal verification defines mathematical properties
of the protocol model.

It does not automatically prove implementation equivalence.
```

```
Formal Protocol Verification
≠
C++ Implementation Equivalence
≠
Production Binary Proof
≠
Complete System Verification
```

```
equivalence_claimed = false
refinement_proved = false
LEVEL_5_CLAIMED=false
CPP_VERIFIED=false
```

This paper defines **verification targets** and **verification
boundaries**. It does not invent new proofs. What is already Closed in
the current kernel cone is listed in `FinalProofInventory.md`
(Inventory Category A). Historical L2 notes under
[`../verification/`](../verification/) do not override that inventory.

---

## 1. Formal verification identity

RIGOR uses formal methods to describe protocol mathematics as an
explicit model: state, transitions, and invariants that can be stated
without referring to a website, an explorer database, or a production
binary.

The **current** machine-checked cone is
`formal_verification/rigor_kernel/` (`_CoqProject.kernel-critical`).
Older L2 files under `formal_verification/coq/` are
`HISTORICAL_ONLY` for public claims.

What the model is for:

| Surface | Role in the model |
|---------|-------------------|
| Protocol state | What consensus is allowed to remember |
| State transitions | Which block/transaction steps are legal |
| Monetary rules | Issuance, destruction, conservation |
| Asset conservation | Unauthorized creation is rejected |
| Issuance constraints | Amounts are functions of consensus inputs |
| Mint-arithmetic properties | Authorization and `R · (2²⁵ + h)² ≤ B << 40` are deterministic |
| Security invariants | Invalid history cannot be accepted |

```
RIGOR Formal Verification Model

=
State Model
+
Transition Rules
+
Monetary Invariants
+
Mint-Arithmetic Properties
+
Security Properties
```

That identity is **model-side**. It does not mean C++ is a refinement
of the model (`TRUE_CPP_REFINEMENT_EXISTS = false`).

---

## 2. Formal state model

**Protocol** consensus state (what the node must remember) is the UTXO
set together with the validation rules that accept or reject a
candidate transition.

```
Protocol consensus state  =  UTXO set + validation rules
```

This is **not** a Coq type name. Same English, different objects
([`../../audit/formal_bridge/ObjectCollisionReport_v1.md`](../../audit/formal_bridge/ObjectCollisionReport_v1.md)):

```
CCoinsViewCache
  ≠ ConsensusModel.UTXOState          (list map; MODEL_PROVED_ONLY)
  ≠ UnifiedState.UTXOState            ({ utxo_wf_flag : bool })
  ≠ UTXOValidator.UTXOState           (alias of tx)
```

Not:

```
Consensus State =
    UTXO Set
  + Explorer Database
  + Global Asset Table
```

RIGOR does not rely on a global account-style asset ledger. Spendable
ownership is represented through UTXO transitions. Script rules determine
classification and authorization boundaries. Explorer indexes and
rebuildable databases are observation, not consensus state.

```
CTxOut = (value, scriptPubKey)

AssetClass = f(scriptPubKey)
```

There is no consensus field `(asset, value, script)`. Asset class is
derived by parsing `scriptPubKey`. Classification is not issuance
authority: recognizing a script family does not create ORU or RES.

```
classification ≠ issuance authority
classifier_refinement = false
```

Authorized issuance is a separate, constrained transition (ORU subsidy
on a valid coinbase; RES mint only on the authorized mint path). Ordinary
spends move existing outputs. `OP_CONVERGE` outputs are not inserted
into the UTXO set.

---

## 3. Core formal properties

These are the major properties **suitable for verification**. Equations
match
[`RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md`](./RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md).

**None** of them is proved of C++, of a production binary, or of the
complete system. Some **are** Inventory Category A facts **inside** the
Coq kernel model. Status words:

| Word | Means |
|------|--------|
| **MODEL_PROVED** | Qed in `rigor_kernel`; not C++ |
| **OPEN** | Not a Closed kernel theorem of this statement |
| **NOT C++** | Must not be advertised as implementation refinement |

---

### 3.1 UTXO conservation property

Ordinary transfers:

```
oru_in ≥ oru_out

res_in ≥ res_out
```

Verification goal:

```
Unauthorized asset creation is impossible
```

Ordinary transfers preserve conservation. Only authorized mint paths
may create RES. ORU subsidy is the authorized coinbase issuance
function `R(h)`, not an arbitrary credit. Fee-destroy never becomes a
UTXO.

**Status:** **MODEL_PROVED** — `economic_no_hidden_creation`, ORU
conservation cluster, `kernel_step_preserves_invariants`. **NOT C++**
(`CCoinsViewCache` inhabit **BLOCKED**).

---

### 3.2 Asset classification property

```
CTxOut ≠ (asset, value, script)
```

Asset class is derived from script. Script classification does not
itself create assets. No hidden asset-state table exists as consensus
truth.

Verification goal: every accepted output is classified by
`f(scriptPubKey)` alone. A model that required a parallel global asset
map would not be this protocol.

**Status:** **MODEL_PROVED** — `classify_total`,
`recognizer_pairwise_disjoint`, spend-by-class (abstract `sig_ok`).
**NOT C++** — `classifier_refinement = false`; not the Script VM;
not Ed25519 EUF-CMA.

---

### 3.3 Deterministic issuance property

ORU:

```
R(h)
```

RES:

```
destroy_cost(h)
```

and the mint predicate

```
R > 0  →  R · (2²⁵ + h)² ≤ B << 40
```

(`CheckResMintArithmetic`). `destroy_cost(h)` is a protocol cost, not a market
price. Pins: `destroy_cost(0) = 102400000000`, `destroy_cost(210000) = 103685749152`,
`destroy_cost(420000) = 104979520049`.
`RES_DESTROY_POLE = 2²⁵`. `RES_COIN = 100000000`.

Verification goal:

```
Same consensus state
+
Same height
+
Same destroy B and mint R
+
Same parameters

=

Same issuance result
```

Issuance is a function of consensus inputs. It is not a miner-chosen
amount, not an explorer balance, not a floating-point policy, and not
a hash lottery.

**Status:** **MODEL_PROVED** — supply schedule / mint-amount bounds in
the kernel model (`oru_subsidy_fold_closed` as `n · 128`, RES mint
bounds). **NOT C++** — not `EvaluateConnect` inhabit; not
`CPP_KERNELINPUT_EQUIVALENCE`.

---

### 3.4 RES supply bound property

```
S_RES(h) ≤ 419430400000000 atoms
```

Verification goal: realised issuance cannot exceed the **closed-form** cap
`RES_MAX_SUPPLY = 419430400000000` (`4194304 RES`). The bound is
`(S / P0) · H = 2²²` RES. It is not a declared 21,000,000.

A non-zero mint occurs only if `h ≥ 0` and
`R · (2²⁵ + h)² ≤ B << 40`. `R = 0` is a legal plain destroy.
The display destroy cost never hits zero; there is no mint horizon.

There is no corresponding absolute ORU cap. `MAX_MONEY` is a per-value
sanity bound, not an ORU supply ceiling.

**Status:** **MODEL_PROVED** — `res_schedule_finite`,
`res_total_issuance_capped` (schedule / aggregate cap). **NOT C++** —
not runtime `S_RES` ConnectBlock rejection (OP-D1).

---

### 3.5 Convergence mint determinism (no draw selection)

Live rule: same-tx destroy-to-mint. There is no draw selection, no σ as a
mint input, no window 32, and no winner comparator.

```
CheckResMintArithmetic(R, B, h)
```

Verification goals:

- deterministic admit / reject from `(R, B, h)`
- no floating-point dependency
- no lottery / `p_draw` / `W(k)` as shipping issuance
- identical inputs produce identical result

Coinbase `0xbb` does not create RES. A body transaction with that script is rejected. A draw is not the issuance rule.

**Status:** **MODEL_PROVED** as the current kernel issuance identity
(no lottery in the shipping model). **NOT C++** inhabit of
`CheckResMintArithmetic`.

---

### 3.6 Proof-of-Work acceptance property

```
pow ≤ target
```

Verification goal: invalid Proof-of-Work states cannot enter **model**
history. Production header acceptance uses **OpuHash**
(`SHA3-256("RGRPOW01" || header_80)`) and ASERT nBits.

In the Coq kernel, the digest is an **uninterpreted** `H`. Integer
ASERT width theorems are Closed. SHA3 / keccak **implementation**
refinement is not proved (`crypto_security = false`).

**Status:** **MODEL_PROVED** — `pow_valid_rejects_above_target`,
`asert_poly_pos` / `asert_poly_fits_uint64` (Coq `Z`). **OPEN** as C++
nBits / OpuHash inhabit. **NOT C++**.

---

## 4. Monetary safety invariants

| Invariant | Statement | Public claim |
|-----------|-----------|----------------|
| **Supply safety** | Unauthorized mint = impossible | MODEL_PROVED in kernel; NOT C++ |
| **Conservation safety** | Valid transition preserves monetary rules | MODEL_PROVED in kernel; NOT C++ |
| **Determinism** | Same input state produces same validation result | MODEL_PROVED for `kernel_step` / checkers; not `ConnectBlock` |
| **Rejection safety** | Invalid transition = rejected | MODEL_PROVED for model accept/reject; not node / P2P |

These invariants are why a closed UTXO **protocol** plus deterministic
functions is amenable to formal reasoning. They are not a claim that
wallets, P2P, or miners are proved.

---

## 5. Verification boundary

Formal verification does **not** prove:

- C++ implementation equivalence (`cpp_equivalence = false`)
- production binary equivalence (`CPP_VERIFIED=false`)
- `ConnectBlock` refinement (`REFINEMENT_PROVED=false`)
- compiler correctness (`compiler_correctness = false`)
- Level 5 (`LEVEL_5_CLAIMED=false`)
- wallet / P2P / miner implementation correctness
- complete blockchain system correctness

```
hash_kernel_shared_binary_with_cpp = false
refinement_proved = false
equivalence_claimed = false
mathematical_documentation_is_code_proof = false
REFINEMENT_PROVED=false
CPP_KERNELINPUT_EQUIVALENCE=false
LEVEL_5_CLAIMED=false
CPP_VERIFIED=false
```

A proved property of the **model** is a proved property of the model.
Correspondence to C++ is a separate, currently unproved obligation.

---

## 6. Relationship between model and implementation

Correct relationship:

```
Formal Model
        |
        v
Protocol Specification Reference
        |
        v
Implementation
```

Incorrect claim:

```
Formal Model = C++ Code
```

Forbidden statements:

- Whitepaper proves C++
- Formal model guarantees implementation correctness
- Mathematical documentation is a code proof
- production C++ node verified
- ConnectBlock refinement proved
- compiler correctness proved
- Level 5 achieved

The mathematical design paper extracts consensus equations. This paper
states which of those equations are verification targets and where the
proof stops. Neither paper is the C++ node.

---

## 7. Verification as security moat

Formal verification is not merely testing. Testing samples behaviors.
Formal protocol-property verification makes critical invariants
explicit and subjects them to machine-checked reasoning **about the
model**.

```
RIGOR trust architecture

=
Dual Native Asset Design
+
UTXO Local State Model
+
Deterministic Monetary Functions
+
Formal Protocol Property Verification
```

That combination is a **protocol trust layer**:

- two native assets with closed script families
- local UTXO state instead of a global account ledger
- issuance and mint arithmetic as deterministic functions
- invariants written so they can be modeled

The trust layer is the *existence and explicitness* of those properties.
It is not a certificate that the production binary matches the model.

---

## 8. Relationship with existing documents

| Document | Purpose |
|----------|---------|
| [`../verification/FORMAL-VERIFICATION-STATUS.md`](../verification/FORMAL-VERIFICATION-STATUS.md) | Production rules and the claim flags |
| [`../../formal_verification/rigor_kernel/release/FinalProofInventory.md`](../../formal_verification/rigor_kernel/release/FinalProofInventory.md) | Classified theorem list emitted by the kernel generator |
| [`chapters/08-formal-boundary.md`](./chapters/08-formal-boundary.md) | Public narrative of the boundary |
| [`RIGOR_PROTOCOL_IDENTITY.md`](./RIGOR_PROTOCOL_IDENTITY.md) | Project identity — what RIGOR is |
| [`RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md`](./RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md) | Mathematical extraction — equations and consensus functions |
| [`../architecture/RIGOR_EXECUTION_MODEL.md`](../architecture/RIGOR_EXECUTION_MODEL.md) | Runtime and execution boundaries |
| [`../formal/VerificationStatus.md`](../formal/VerificationStatus.md) | Public flags. A model proof is not a proof of the node |
| [`../explorer/EXPLORER_ARCHITECTURE.md`](../explorer/EXPLORER_ARCHITECTURE.md) | Explorer implementation — observation, not consensus |

Explorer remains an independent product. Formal verification does not
make an indexer into a ledger of record.

---

## 9. Final identity statement

```
RIGOR Security Identity

=
Mathematical Consensus Design
+
Formal Protocol Properties (Coq model)
+
Explicit Verification Boundaries
```

ALLOWED:

```
Coq consensus kernel model has formally verified invariants.
```

NOT ALLOWED:

```
production C++ node verified
ConnectBlock refinement proved
compiler correctness proved
Level 5 achieved
```

```
MATURITY_LEVEL=4.9
equivalence_claimed = false
hash_kernel_shared_binary_with_cpp = false
refinement_proved = false
REFINEMENT_PROVED=false
CPP_KERNELINPUT_EQUIVALENCE=false
LEVEL_5_CLAIMED=false
CPP_VERIFIED=false
mathematical_documentation_is_code_proof = false
```
