# Copies that are not the production consensus

This directory holds copies of Coq text. They are not the RIGOR issuance rule and they are not a proof of the C++ node.

Production issuance is RFC-020: a body transaction destroys ORU and may mint RES when `R · (2^25 + h)^2 ≤ B << 40`. Production proof-of-work is FIPS SHA3-256(`"RGRPOW01" || header_80`).

| Copy | What it is |
|------|------------|
| `coq/CvgCovenant.v` | A covenant-claim model. The production classifier does not accept that claim as a spend path. |
| `coq/legacy_account_model/README.md` | Points at an account-style model. The node is a UTXO ledger. |

`equivalence_claimed` and `refinement_proved` are false.
