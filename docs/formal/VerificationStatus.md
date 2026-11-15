# Formal verification status

**Status:** Public formal boundary (index).  
**Claim boundary:** the flags in this file. Production issuance is RFC-020. A model proof is not a proof of the C++ node.

```
REFINEMENT_PROVED=false
CPP_KERNELINPUT_EQUIVALENCE=false
LEVEL_5_CLAIMED=false
CPP_VERIFIED=false
equivalence_claimed=false
```

`formal_verification/rigor_kernel/release/FinalProofInventory.md` prints `MATURITY_LEVEL=4.9`. `LevelDefinition.md` in that directory defines the number as a label for the model package: a runtime domain, a source lock, and classification coverage of the Coq cone. The same definition says level 5 would require a proved extraction and a verified compiler, and it says this package does not claim those. The label is not a result about the C++ node.

Formal verification proves properties of the protocol **model**. This
repository does not claim that production C++ is a refinement of that
model.

## Allowed public sentence

> Formally verified critical consensus invariants inside a Coq consensus
> kernel model, with explicit extraction and runtime trust boundaries.

## Forbidden sentences

- production C++ node verified
- ConnectBlock refinement proved
- compiler correctness proved
- Level 5 achieved
- `CPP_VERIFIED`
- `LEVEL_5` as a current achievement

## Where to read more

| Document | Role |
|----------|------|
| [`Claims.md`](./Claims.md) | Yes / no release sentences |
| [`../../formal_verification/README.md`](../../formal_verification/README.md) | Operator map for the Coq tree |
| [`../../formal_verification/rigor_kernel/release/FinalProofInventory.md`](../../formal_verification/rigor_kernel/release/FinalProofInventory.md) | Inventory emitted by the kernel tree |

The flags in this file are the public claim boundary. The inventory label described above is not a proof that the C++ node matches the model.
