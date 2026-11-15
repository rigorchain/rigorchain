# Formal verification (public)

Active formal documents in this folder:

| Document | Role |
|----------|------|
| [`VerificationStatus.md`](./VerificationStatus.md) | Current flags and public sentence |
| [`Claims.md`](./Claims.md) | Allowed / forbidden claims |
| [`SSOT_POLICY.md`](./SSOT_POLICY.md) | Protocol SSOT = RFC; C++ is realization |
| [`SSOT_HIERARCHY.md`](./SSOT_HIERARCHY.md) | Protocol > implementation > evidence > presentation |

Authoritative machine-facing files:

- [`../../audit/formal_bridge/FORMAL_STATUS_SOURCE_OF_TRUTH_v1.md`](../../audit/formal_bridge/FORMAL_STATUS_SOURCE_OF_TRUTH_v1.md)
- [`../../audit/formal_bridge/ReleaseClaimBoundary_v1.md`](../../audit/formal_bridge/ReleaseClaimBoundary_v1.md)
- [`../../formal_verification/rigor_kernel/release/`](../../formal_verification/rigor_kernel/release/)

```
equivalence_claimed = false
refinement_proved = false
LEVEL_5_CLAIMED = false
CPP_VERIFIED = false
```

This directory does not contain a separate historical-report tree.
The claim flags in [`VerificationStatus.md`](./VerificationStatus.md) are the public boundary.
