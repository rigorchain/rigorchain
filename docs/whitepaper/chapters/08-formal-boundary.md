# Chapter 8 — The Formal Boundary: The Model Is Not the Running Binary

Chapter 7 requires local recomputation. What is recomputed is the
bytes the node is checking. What executes them is the C++ node. This
chapter states the current final formal conclusion: one public
sentence; “yes” stops inside the model.

The claim is architectural. The protocol publishes **what is proved**
and **what is not**. It does not read a green build as “the chain has
been formally proved.”

```
formal verification of properties of the protocol model
        ≠
C++ implementation equivalence
        ≠
running-binary proof
        ≠
complete system verification
```

The verified object is the mathematical model. The running
implementation remains an engineering artifact subject to
implementation verification. See
[`RIGOR_FORMAL_VERIFICATION_DESIGN.md`](../RIGOR_FORMAL_VERIFICATION_DESIGN.md)
and the living inventory cited in §8.2.

The final conclusion is stated once, in §8.5. Earlier sections state
the boundary. They do not add a second publicity line.

---

## 8.1 Why the boundary is drawn

A theorem about a mathematical model does not automatically become a
theorem about every line of node code, every network path, or every
wallet. If the three layers are read as one “fully verified” sentence,
a green Coq build and a green differential gate get read as
`equivalence_claimed = true`. This chapter does not issue that
statement.

The boundary keeps the rules in earlier chapters checkable without
turning the check into an authority:

| Layer | What it is | What it is not |
|-------|------------|----------------|
| Running consensus | The C++ node `core/src/`. Code is the SSOT | RFC, SPEC, or Coq rewriting the running rules |
| Coq kernel model | Machine-checked properties of the model | A C++ refinement proof; `ConnectBlock` |
| Differential / extraction | Finite golden vectors, source identity, a declared runtime path | Universal equivalence; a verified compiler in the current TCB |

---

## 8.2 Scale is recorded in the inventory, not in this paper

The kernel lives in `formal_verification/rigor_kernel/`. Theorem
counts, category splits, maturity scores, mutation-gate lists, and
extractor inventories are **dynamic engineering state**. They are not
protocol parameters and they are not part of the public argument of
this paper.

The living records are:

- classified inventory: `release/FinalProofInventory.md`
- mutation gate: `release/MutationCompletenessAudit.md`
- public page: `/documentation/formal-verification`

Those files report, at the time they were generated, how many
statements are classified, which are Category A (citable core
invariants), which are supporting lemmas, and which are explicit
non-claims. This chapter does not freeze those figures. Reading any
day’s tally as “the running node has that many proved rules”
collapses the three layers again.

Classification rule, independent of the day’s count: only Category A
enters a public technical citation. Supporting lemmas should not be
quoted as “that domain is proved.” Boundary flags remain non-claims,
including the honesty flags themselves.

---

## 8.3 Side-by-side reading is not refinement

The names that receive “yes” in §8.5 share names with rules in this
paper, so they can be read side by side. Side-by-side reading is not
a refinement proof. They do not let anyone skip Chapter 7. Even if
the model answers “yes,” the bytes the node is checking still have to
match the commitment.

Hash in the model is an uninterpreted symbol `H`. Theorems about
SHA3 or Ed25519 **security** do not exist. Abstract `sig_ok` is not
unforgeability.

---

## 8.4 What sits explicitly outside the boundary

The following are **not** proved. That is not “not yet in an
appendix.” It is this chapter’s boundary:

- Universal C++ ↔ Coq ↔ Python equivalence
- That `ConnectBlock` refines the model transition
- That extracted code computes the Coq function
- Compiler correctness (`ocamlopt` is trusted)
- k-deep confirmation / honest majority (a work race, not an issuance
  identity)
- ASERT’s envelope versus `2ˣ`
- Security of cryptographic primitives beyond an executable hash
  model and vector checks
- Hardware, side channels, P2P honesty, mempool, wallet assumptions

The differential gate connects specification and implementation with
finite golden vectors: a locked case that regresses fails the build or
the gate. Finite agreement catches regressions. It cannot rewrite
“these points agree” as “every point is equivalent.”

Testing is not proof. Mutation analysis can show that a theorem breaks
when the model is sabotaged. It cannot show that the running node
equals that theorem.

---

## 8.5 The final formal conclusion

The only sentence that may be used in public:

> Inside the Coq consensus kernel model, critical consensus
> invariants have been machine-checked, with explicit extraction
> and runtime trust boundaries.

What may be answered **yes**: selected invariants inside this Coq
model hold — for example kernel soundness, ORU conservation, the RES
ceiling, classifier partition, and chain induction. `Print
Assumptions` status for the public classification is recorded in the
inventory, not restated here as a frozen count.

What may not be answered **yes**: that the running C++ node equals
the model.

The public sentence must not collapse into “the chain has been
proved,” “bug free,” or “no need to recompute.” The honesty flags
stay false until a refinement proof rewrites them.

---

## 8.6 What this chapter does not claim

- It does not claim Level 5, or a verified compiler in the current
  TCB.
- It does not write a day’s theorem count or mutation count as a
  protocol parameter.
- It does not write a research archive or an experiment page as an
  equivalence proof.
- It does not write “the model is strongly verified” as “mainnet is
  complete.”
- It does not claim that C++ is formally verified, or that the
  running chain is a formally verified chain.

Living correspondence:
`docs/verification/FORMAL-VERIFICATION-STATUS.md`. Reproduce issuance and proof-of-work from the node against RFC-020 and SPEC-008.
Engineering frontier:
`formal_verification/rigor_kernel/release/FormalizationFrontier.md`.
Classified inventory:
`formal_verification/rigor_kernel/release/FinalProofInventory.md`.

**What is proved within the stated model is the model. What runs is C++. The two have not
been proved to be the same thing.** Chapter 9 states how product
surfaces live with these rules — a display remains a display, and
should not read Chapter 8 as “the chain has been proved.”
