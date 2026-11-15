# Formal verification

`equivalence_claimed` is **false**. This directory does not claim that the Coq model is a refinement of the C++ node.

Production issuance is RFC-020: a body transaction destroys ORU and may mint RES under `R · (2²⁵ + h)² ≤ B << 40`. Production proof-of-work is FIPS SHA3-256(`"RGRPOW01" || header_80`).

| Document | Role |
|----------|------|
| [FORMAL-VERIFICATION-STATUS.md](./FORMAL-VERIFICATION-STATUS.md) | What production implements, and what a model does not prove |
| [FV-RELEASE-CHECKLIST.md](./FV-RELEASE-CHECKLIST.md) | Commands recorded for a correspondence run, and what a green line does not mean |
| [`../formal/VerificationStatus.md`](../formal/VerificationStatus.md) | Public claim flags |

Machine-checked sources: [`../../formal_verification/`](../../formal_verification/).
