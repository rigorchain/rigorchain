# RES issuance checks

Production RES issuance is the RFC-020 predicate in `core/src/consensus/res_issuance.h`:

```
R · (2^25 + h)^2 ≤ B << 40
```

`ComputeSigmaHash`, `IsEligible`, `IsBetter`, and a draw-winner test are not that rule. They are not acceptance tests for the production node. `ComputeSigmaHash` is not defined under `core/src/`.

Block validation does not consult those functions.
