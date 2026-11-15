# Legacy account-model Coq kernel (W9)

These files are a global account model. They are not the C++ consensus model.

```
equivalence_claimed = false
hash_kernel_shared_binary_with_cpp = false
refinement_proved = false
```

These files (`Types.v`, `State.v`, `Transition.v`, `Selection.v`) use a **global account model**: chain-wide `ORU_supply` / `RES_supply` scalars and a list-of-tx `Block`. They **do not** match the current C++ implementation, which is UTXO-based (`CCoinsViewCache`, per-output asset class, ConnectBlock mint coupling in `rigor::ConnectEvaluateResMintTx`).

Do not cite theorems from this directory as proofs about:

- script evaluation (`EvalScript`)
- RES mint authorization
- dual-pool UTXO conservation
- reorg / undo

## Where to look instead

The structural Coq model that tracks the C++ *shape* (still **not** a refinement proof) is:

- [`../ReplayReorgCore/ReplayState.v`](../ReplayReorgCore/ReplayState.v)
- [`../ReplayReorgCore/StateTransition.v`](../ReplayReorgCore/StateTransition.v)
- [`../ReplayReorgCore/DualPoolConservation.v`](../ReplayReorgCore/DualPoolConservation.v)
- [`../ReplayReorgCore/ConnectBlockMint.v`](../ReplayReorgCore/ConnectBlockMint.v)

Honesty flags for ReplayReorgCore remain `false`. That kernel is a replay/reorg *structure*, not a claim that Coq and C++ share a binary hash or that refinement is proved.

## Why these files still compile

Downstream modules (`Kernel/`, `Proofs/`, `Execution.v`, …) still `Require Import Rigor.legacy_account_model.*` so historical selection lemmas keep building. That import path is the deprecation: it is no longer the root `Rigor.Types` / `Rigor.State` of a supposed UTXO kernel.

Regenerate the Coq makefile after path changes:

```bash
cd formal_verification/coq
coq_makefile -f _CoqProject -o Makefile.coq
```
