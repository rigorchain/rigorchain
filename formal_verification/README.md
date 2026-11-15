# Rigor Consensus Formal Verification

> **Scope statement (read this first).**
> This directory contains a Coq corpus and remaining mint / hash / classify
> differential tests. The lottery Python L1 spec was removed in Batch A. It does **not** contain a proof
> that the C++ implementation matches the specification, and it does **not**
> contain a no-fork theorem. Equivalence between the three layers is **NOT
> ESTABLISHED**. Every claim below is accompanied by the file and line where it
> can be checked; if a claim here cannot be located in the source, treat the
> claim as a documentation bug and file it.

**Authority rule:** Protocol specifications are normative SSOT.
This directory is a formal **model**, not C++ authority. If this README
disagrees with RFC-020 / RFC-018, the RFC is correct.

**Current public status:** [`docs/formal/VerificationStatus.md`](../docs/formal/VerificationStatus.md).
Claim boundary: [`docs/formal/Claims.md`](../docs/formal/Claims.md).

```
equivalence_claimed = false
hash_kernel_shared_binary_with_cpp = false
refinement_proved = false
```

**Product identity (P0-029):** canonical tree is
[`https://github.com/rigorchain/rigorchain`](https://github.com/rigorchain/rigorchain).
Wallet storage is a **SQLite descriptor** file named `rigor-wallet.sqlite`
(not Berkeley DB, not `wallet.dat`). There is **no** `libbitcoinconsensus`
(or `librigorconsensus`) C ABI in this tree — consensus verify is in-process
`VerifyRigorPredicate` after `AssetTypeFromScriptPubKey`. The spend path is Ed25519. It is not a general script interpreter.

| Public document | Path |
|-----------------|------|
| Formal verification status | [`docs/formal/VerificationStatus.md`](../docs/formal/VerificationStatus.md) |
| Claims | [`docs/formal/Claims.md`](../docs/formal/Claims.md) |
| FV release checklist | [`docs/verification/FV-RELEASE-CHECKLIST.md`](../docs/verification/FV-RELEASE-CHECKLIST.md) |

---

## 1. The three layers, and what actually connects them

| Layer | Artifact | Status |
|---|---|---|
| L1 — Executable spec | Production issuance is RFC-020 in the C++ node. This directory does not add a second issuance rule. | |
| L2 — Coq model | `coq/**` (~218 files) | Machine-checked model properties. Not C++ equivalence. |
| L3 — C++ production | `core/src/**` | Realization of RFC-020 / RFC-018. Tests are evidence, not a proof. |

Connections that actually exist:

- **L1 ↔ L2**: Lottery L1 spec / `GoldenHarness.v` / `gen_coq_harness.py` were
  removed in Batch A. Remaining mint / script / hash rows live in
  `phase3_golden_vectors.json` (127) and `coq/Kernel/NegativeCoreHarness.v`.
  FIPS short SHA-256 is locked in `CanonicalHashKernel.v`. Vector-level
  agreement only; not universally quantified equivalence.
- **L1 ↔ L3**: lottery `selection_golden_bridge.inc` removed. Remaining hash
  evidence is the portable CSHA256 CLI. Runtime harness JSON still carries
  `equivalence_claimed: false`.
- **L2 ↔ L3**: **no refinement proof.** Coq SHA-256 can be extracted to OCaml
  (`extracted/hash256_kernel.ml`); portable C++ CLIs compile `sha256.cpp`
  for the gate. Those are test drivers, not a shared node binary.
  What exists toward C++ is differential testing over the shared vectors
  plus a canonical encoder spec both sides implement. PD-4F-CPP-01 remains open.

There is **no** theorem anywhere in `coq/` of the form "C++ = Python = Coq".
Any document asserting one is wrong.

---

## 2. What a Coq file in this tree is

A theorem in this directory is a statement about the Coq file that contains it. It is not a statement that `core/src/` implements that file. `equivalence_claimed` and `refinement_proved` are false. This page does not record a rebuild.

Production proof-of-work is FIPS SHA3-256(`"RGRPOW01" || header_80`) in `GetRigorPowHash`. A Coq development of SHA-256 or SHA256d is not that digest.

`formal_verification/golden_lock.json` records `shared_vectors` = 127 and `coq_enforced_lemmas` = 75. Those are fields in the file. They are not a count of proofs executed for this page, and they are not 422.

Files under `coq/` that define a winner, a draw candidate root, or a σ preimage describe that model. Production issuance does not use them. Do not cite those lemmas as the mint rule or as a proof of the node.

### 2.1 UTXO and replay/reorg in the model

- `coq/ReplayReorgCore/ReplayInvariants.v` — `utxo_spend_removes_output`,
  with `not_recreated` independent of in-block index. Distinct-txid spends
  delete unconditionally (`utxo_spend_removes_output_distinct_txid`). See §3.
- `coq/ReplayReorgCore/ReplayInvariants.v:190` —
  `utxo_spend_removes_output_unconditional_is_false`: a machine-checked
  *refutation* of the unconditional form.
- `coq/ReplayReorgCore/ReplayInvariants.v:230` — `rollback_restores_previous_state`.
- `coq/ReplayReorgCore/ReplayInvariants.v:256` — `replay_preserves_utxo_consistency`
  (same side condition).
- `coq/ReplayReorgCore/SupplyConservation.v:245,311,411` — `supply_monotonic`,
  `rollback_preserves_supply`, `genesis_supply_zero`.

### 2.2 Selection files are not the production mint rule

`coq/Proofs/Determinism.v`, `coq/Proofs/Fairness.v`, `coq/Proofs/Safety.v`, and `coq/DrawCandidateRoot.v` talk about a selection function and a winner. Those statements are about those files. They are not `CheckResMintArithmetic`, and they are not a proof of the node.

`coq/Phase4GRefinement/FormalVerificationCertification.v` states that a release label in that file does not mean Coq equals the C++ node.

---

## 3. Known-false and explicitly-open statements

These are recorded here so nobody re-derives a false claim from an old document.

**A spent outpoint is not unconditionally removed under txid reuse.**
`apply_tx_utxo` now assigns created outputs `MkOutPoint (tx_id tx) i`
(vout starting at 0). Distinct transactions cannot collide. The
unconditional lemma remains refutable when a transaction's `tx_id`
equals the txid of an outpoint it spends (duplicate txid / BIP30-class).
The refutation is checked by the build
(`coq/ReplayReorgCore/ReplayInvariants.v`, `utxo_spend_removes_output_unconditional_is_false`).
`utxo_spend_removes_output_distinct_txid` is a lemma about that Coq function. It is not a proof that the node implements the function.

**Open proof obligations (unchanged, restated):** PD-4F-OB-02 (oracle agreement /
C++ bridge), PD-4F-CPP-01 (C++ equivalence), PD-4F-OB-03. Under SECURITY-001-F the
former `True`-bodied placeholder theorems in `ConsensusDeterminism.v:69`,
`ConsensusOracleBridge.v`, and `ConsensusTraceExecution.v` were replaced by
explicit `Prop` definitions naming the obligation plus a provable conditional
theorem, so the obligation is now visible in the source rather than disguised as
a discharged result.

---

## 4. Verification gap register (summary)

Equivalence is unproved. The boundary is `docs/verification/FORMAL-VERIFICATION-STATUS.md`. A row about a draw or a winner is a statement about a Coq file. It is not the production mint rule.

| # | Gap | Why it matters |
|---|---|---|
| G-1 | `OrderIndependence.v` states order properties of a draw set and a winner inside that file. Production minting has no winner. The lemma is not a proof of the node. | Not a production rule. |
| G-2 | **No absolute ORU supply cap.** `cb_oru_subsidy_ok` bounds per-block ORU; RES has `max_issuance_within_cap` and `valid_res_cap` reject. The 125 ORU tail means `total_oru_supply ≤ ORU_MAX_SUPPLY` is **false by design**. | Residual is the tail, not unbounded model issuance. |
| G-3 | **No Coq↔C++ equivalence.** Hash-kernel extraction exists; there is still no refinement proof. | All Coq results are about the model only. |
| G-4 | **Vector-level, not universal, L1↔L2 agreement.** 422 shared / 215 Coq-enforced (was 25 in the original audit). | Sound but finite. Not equivalence. |
| G-5 | **Txid-reuse residual** in the replay model (§3). The shared-placeholder shortcut is closed. | Unconditional spend-delete is still false if a tx reuses a spent outpoint's txid. |
| G-6 | **Residual opaque boundaries** (`OpaqueBoundary.v`, `DrawCandidateRoot.v` §9). Script, `cm_draw_won`, DCR coinbase bind, and `rs_res_supply` vs `utxo_res` are declared non-claims. | Prevents reading Closed mint/dual-pool theorems as owner/PCORB or script-derived assets. |

---

## 5. Building and running

### Coq corpus

```bash
cd formal_verification/coq
coq_makefile -f _CoqProject -o Makefile.coq
make -f Makefile.coq -j"$(nproc)"
```

Lottery `Kernel/GoldenHarness.v` is gone. Compile remaining protocol-core
files from `_CoqProject.protocol-core`. There is no `ArdaConsensus.v`.

### Assumption audit (what rests on axioms)

```bash
cd formal_verification
python3 tools/audit_assumptions.py
```

Runs `Print Assumptions` on every top-level `Theorem`/`Lemma`/`Corollary` and
emits the axiom-free vs axiom-dependent split. Run this before citing any
theorem as established.

### Python spec (L1)

Production issuance is RFC-020 in `core/src/consensus/res_issuance.h`. A Python file in this tree is not a second consensus rule.

### Differential gate / pre-release checklist

```bash
cd ~/rigor-official-tree
bash formal_verification/scripts/run_fv_release_checklist.sh
bash formal_verification/scripts/run_differential_gate.sh --help
```

The checklist runner is freeze artifacts plus `SKIP_TEST_RIGOR=1` `run_differential_gate.sh` (lock check is gate step 2b). Optional: `FV_COQ=1`, `FV_EXTRACT=1`, `FV_TEST_RIGOR=1`. Direct gate: `bash formal_verification/scripts/run_differential_gate.sh`. This is not a refinement proof. See [`docs/verification/FV-RELEASE-CHECKLIST.md`](../docs/verification/FV-RELEASE-CHECKLIST.md).

Lottery golden-vector oracles and the σ differential fuzzer were removed in
Batch A. Remaining hash rows: `python3 tests/test_hash_kernel_agreement.py`.

---

## 6. Byte-level requirements (normative, from L1)

These are requirements on implementations, not proven theorems.

- SHA256d exactly; no variants, no single-SHA.
- σ preimage is **100 bytes**: `txid ‖ C ‖ B_{h-1} ‖ LE32(h)`. Height is LE32 at offset 96.
  There is **no** `target_prev` slot (`canonical_encoder_has_no_target_prev_slot`).
- txids sort by **memcmp byte order** — never numeric, never string-lexicographic.
- ConvergenceRoot is a **flat concatenation** of sorted txids then SHA256d — not a
  Merkle tree, and individual txids are not pre-hashed.
- Empty F ⇒ ConvergenceRoot is 32 zero bytes (proven in the model:
  `coq/Kernel/KernelRoot.v` `converge_root_empty`). A singleton hashes
  `Hash256(txid)`, same as any larger F.
- `draw_candidate_root` is the same concat-hash; the remaining distinction is
  the empty set (DCR hashes `[]`, C is zeros). See `coq/DrawCandidateRoot.v`.
  Coinbase bind of that digest is C++ (`ComputeConvergenceDrawCandidateRoot`).
- Tie-break is **txid memcmp** (unsigned byte order) after equal cross-products.
  It is **not** `txid XOR ConvergenceRoot` (removed; RFC-000 §9).
- Inequality is strict: `σ < T · f` (`eligible` / `ConvergenceDrawInequalityHolds`).
  `f` is on-chain weight in base units; `T` is `RES_PROBABILITY_TARGET` (E=246, ÷ W=32).

Historical fork incidents motivating each rule (endianness, sort order, hash
variant, Merkle variant, tie-break ambiguity) are catalogued in the L1 spec
comments; they are context, not evidence about this codebase.

---

## 7. For auditors

1. Start with RFC-020 and `core/src/consensus/res_issuance.h`.
2. Do not treat a Coq selection lemma as the mint rule.
3. `equivalence_claimed` stays false. A golden file is not a proof that the node matches the model.
4. Read §3 and §4 before reading any other verification document in this repo.
5. Assume Coq↔C++ equivalence is **unproven** unless you find a refinement proof.
   As of this writing there is an optional OCaml *extraction* of the Coq hash
   kernel (`extracted/`), which is not a C++ correspondence proof. Script
   classification is modeled as abstract byte-shape predicates
   (`ScriptMintWitness.v`); a **finite shape subset** is a conjunct of
   `apply_tx` (`ScriptShape.v`). `IsResScript` vs `out_asset` is not.
   Production minting does not bind a draw candidate root.
