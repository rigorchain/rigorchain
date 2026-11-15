# Formal verification status

Production consensus is the C++ validation of RFC-020 and RFC-018. RES minting is the deterministic inequality `R · (2²⁵ + h)² ≤ B << 40` on a body transaction. There is no draw and no σ input to issuance.

```
equivalence_claimed = false
hash_kernel_shared_binary_with_cpp = false
refinement_proved = false
LEVEL_5_CLAIMED = false
CPP_VERIFIED = false
```

A green correspondence run does not prove that the Coq model and this node implement the same function.

## What this tree can support

`formal_verification/golden_lock.json` records `shared_vectors` = 127. That integer equals the sum of `families.*.vectors` in the same file. `coq_enforced_lemmas` in the same file is 75. This page does not record a run of those vectors, and 127 is not a count of passing proofs.

`ComputeSigmaHash` is not in `core/src/`. A production σ / root CLI is not part of block validation.

The public claim boundary for readers of the protocol papers is [`../formal/VerificationStatus.md`](../formal/VerificationStatus.md).

## Operator entry

Pre-release commands, and the statement of what a green run does not mean, are in [`FV-RELEASE-CHECKLIST.md`](./FV-RELEASE-CHECKLIST.md). Honesty flags stay false.

Machine-checked sources live under [`../../formal_verification/`](../../formal_verification/). Theorem comments in that tree are evidence about the model they name. They are not a second consensus rule.
