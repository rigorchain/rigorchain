> **P9A note:** Human-facing canonical terminology (ORU / RES / forbidden legacy codenames) is frozen in
> ``docs/freeze/PROTOCOL_NAMING_TRUTH_SOURCE.md``.
> **This file** remains the engineering source of truth for **`bad-*` reject strings** and stable RPC keys.

# RIGOR protocol naming — source of truth (freeze prep)

This file is the **authoritative reference** for consensus-visible **reject reason strings** (`bad-*`), **replay / state hash** identifiers, and **stable RPC JSON keys** that appear in node output.  
Use the current C++ literals in `core/src` and the tables below. Older documents are not the truth for these identifiers.

**Regenerate / verify** (from repository root, after edits under `core/src`):

```bash
rg 'Invalid\([^)]*,\s*"bad-' core/src --glob '*.cpp' -N
rg '"bad-[a-z0-9-]+"' core/src --glob '*.cpp' -N
```

Primary implementation files for `bad-*` literals in this tree:

- `core/src/validation.cpp`
- `core/src/connect_block_rigor_checks.cpp`
- `core/src/consensus/tx_check.cpp`
- `core/src/consensus/tx_verify.cpp`
- `core/src/consensus/rigor_res.cpp`

---

## 1. Authoritative `bad-*` reject reasons (block + tx validation)

Sorted **unique** literals taken from `state.Invalid(..., "bad-…")` in the files above (commit at time of authoring; re-run §0 commands after merges).

| Reject reason |
|---------------|
| `bad-blk-length` |
| `bad-blk-sigops` |
| `bad-blk-weight` |
| `bad-cb-amount` |
| `bad-cb-convergence-root` |
| `bad-cb-length` |
| `bad-cb-missing` |
| `bad-cb-multiple` |
| `bad-cb-res-amount` |
| `bad-convergence-missing` |
| `bad-converge-amount` |
| `bad-converge-overflow` |
| `bad-diffbits` |
| `bad-fork-prior-to-checkpoint` |
| `bad-genesis-convergence-commitment` |
| `bad-genesis-convergence-txns` |
| `bad-prevblk` |
| `bad-rigor-commitment-missing` |
| `bad-rigor-convergence-root-mismatch` |
| `bad-rigor-mixed-asset-inputs` |
| `bad-rigor-multiple-res` |
| `bad-rigor-res-amount` |
| `bad-rigor-res-binding` |
| `bad-rigor-res-mint-domain` |
| `bad-rigor-res-proof` |
| `bad-rigor-res-trigger-underflow` |
| `bad-rigor-res-witness` |
| `bad-rigor-res-without-winner` |
| `bad-rigor-res-winner-mismatch` |
| `bad-rigor-script-type` |
| `bad-rigor-target-mismatch` |
| `bad-signet-blksig` |
| `bad-sigma-convergence-not-in-convergence-root-set` |
| `bad-txnmrklroot` |
| `bad-txns-BIP30` |
| `bad-txns-accumulated-fee-outofrange` |
| `bad-txns-asset-script-nonstandard` |
| `bad-txns-duplicate` |
| `bad-txns-fee-domain` |
| `bad-txns-fee-outofrange` |
| `bad-txns-inputs-duplicate` |
| `bad-txns-inputs-missingorspent` |
| `bad-txns-inputvalues-outofrange` |
| `bad-txns-nonfinal` |
| `bad-txns-nonstandard-inputs` |
| `bad-txns-opreturn-value` |
| `bad-txns-orum-in-belowout` |
| `bad-txns-oversize` |
| `bad-txns-premature-spend-of-coinbase` |
| `bad-txns-premature-spend-of-res-mint` |
| `bad-txns-prevout-null` |
| `bad-txns-res-amount-invalid` |
| `bad-txns-res-fee-invalid` |
| `bad-txns-res-in-belowout` |
| `bad-txns-res-input-invalid` |
| `bad-txns-res-output-nonpositive` |
| `bad-txns-spend-convergence` |
| `bad-txns-spend-non-asset-input` |
| `bad-txns-spends-conflicting-tx` |
| `bad-txns-too-many-sigops` |
| `bad-txns-txouttotal-toolarge` |
| `bad-txns-vin-empty` |
| `bad-txns-vout-empty` |
| `bad-txns-vout-negative` |
| `bad-txns-vout-toolarge` |
| `bad-witness-merkle-match` |
| `bad-witness-nonce-size` |
| `bad-witness-nonstandard` |

### Dynamic / formatted reasons (still authoritative patterns)

| Pattern | Notes |
|---------|--------|
| `bad-version(0x…​)` | Produced via `strprintf("bad-version(0x%08x)", block.nVersion)` in `validation.cpp`. |
| `bad-cb-convergence-root` | Second diagnostic token may carry a nested string from convergence parsing (`convergence_err`). |

---

## 2. Replay + state hash + RPL2 (frozen identifiers)

| Identifier | Role |
|------------|------|
| `state_hash` | **Authoritative** deterministic chainstate digest (`Consensus::ComputeDeterministicStateHash`). Exposed on RPC (e.g. `getdeterministicstatehash`, `getreplaysnapshot`, chainstate summaries). |
| `ComputeDeterministicStateHash` | Implementation entry; preimage definition lives with consensus state snapshot code (`state_snapshot.h` / `validation.cpp`). |
| `RPL2` | **Wire magic** for diagnostic replay snapshots (`Consensus::ReplaySnapshot::kWireMagic`). Serialized form is **not** interchangeable with legacy RPL1 (explicitly rejected). |
| `serialized_snapshot` | RPC hex field carrying the canonical RPL2 blob (`getreplaysnapshot`). |
| `getreplaysnapshot` | RPC method name (diagnostic; still a stable external surface once published). |
| `getdeterministicstatehash` | RPC method name for the digest alone. |

Normative comments: `core/src/consensus/replay_snapshot.h`, `core/src/consensus/state_diff.h`, `core/src/rpc/blockchain.cpp` (`getreplaysnapshot` / `getdeterministicstatehash`).

---

## 3. Stable RPC JSON keys (RIGOR / convergence / RES — non-exhaustive)

These keys are emitted by `core/src/rpc/blockchain.cpp` (and related RPC). Treat as **stable** unless an RFC + release explicitly deprecates them.

| Key | Typical context |
|-----|-----------------|
| `destroy_cost` | `getconvergencestatus`. Display cost at `confirm_height`. Not the mint predicate input |
| `cumulative_convergence` | `getblockconvergence` and `getconvergencestatus`. Destroyed ORU total |
| `res_balance` | snapshots / summaries |
| `res_total` | cumulative RES mint views |
| `convergence_root_set_size` | `getblockconvergence` |
| `convergence_amount` | per-candidate objects |
| `convergence_type` | per-candidate objects |
| `state_hash` | replay / chainstate diagnostics |
| `serialized_snapshot` | RPL2 hex (`getreplaysnapshot`) |

**Wallet / PSBT extension** (wallet RPC & PSBT helpers): keys prefixed `res_v1_*` in `core/src/rpc/psbt_res_compat.cpp` — stable for tooling that parses PSBT JSON.

**Wallet RPC method names** registered in this tree include `createconvergence` (`core/src/wallet/rpc/rpcspend.cpp`). Blockchain RPC names registered in this tree include `getblockconvergence`, `getconvergencestatus` (`core/src/rpc/blockchain.cpp`), and `listconvergencehistory` (`core/src/rpc/rawtransaction.cpp`). `getconvergenceresult` is not registered.

---

## 4. CLI debug flag (operational surface)

| Flag | Purpose |
|------|---------|
| `-debugrigorpow` | Verbose RIGOR PoW / connect-block diagnostics (`init.cpp`, `validation.cpp`, `pow_opus.cpp`). **Stable for scripts** until an RFC introduces an alias and deprecation window. |

---

## 5. Non-authoritative / historical strings (do not cite as current node `bad-*`)

The following appear in **older docs**, **formal_verification fixtures**, or **tests** but are **not** produced by the current `state.Invalid(..., "bad-…")` set in §1:

- **`bad-convergence-cumulative`**, **`bad-convergence-draw-candidate-root`** — retired with the CVGE coinbase `OP_RETURN`. Do not reintroduce. Window C lives in the coinbase `scriptSig` (`bad-cb-convergence-root`).
- Strings such as **`mixed-asset-output`**, **`convergence-must-be-orum`**, **`invalid-convergence-structure`** — these occur in **unit tests** / other validation paths, not necessarily as the same `TxValidationState` / `BlockValidationState` reject token as §1.

When updating docs or formal specs, **map** old names to §1 using a real node trace or the source lines referenced in §0.

---

## 6. Governance rule (minimal)

1. **RFCs and new markdown** MUST use §1–§4 as the vocabulary for identifiers that appear on the wire, in RPC JSON, or in reject reasons.  
2. Any new reject reason MUST be added to this file in the same commit as the C++ literal (or immediately after in a docs-only follow-up).  
3. Renaming or removing an entry in §1–§4 is a **consensus / compatibility** change and requires its own RFC + release notes — never a silent editorial fix.
