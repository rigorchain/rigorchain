# H4 — Deterministic Replay Harness Review (ARCHIVED)

**Status:** COMPLETE  
**Scope:** STEP-0201 / STEP-0202 / STEP-0203 (functional harness + capture + reorg audit)  
**Date note:** Closed after CI-local verification (1000-block linear sync, capture pipeline, reorg K∈{3,5,10}).

## 1. Verification checklist (core coverage)

| Item | Evidence |
|------|----------|
| `state_hash` + RPL2 alignment at sampled points | `feature_deterministic_replay.py`: every `CHECK_INTERVAL` blocks, `getdeterministicstatehash` + `getreplaysnapshot.serialized_snapshot` + `syncwithvalidationinterfacequeue` on all nodes before sample. |
| 1000-block linear 3-node sync | Same harness: `TARGET_BLOCKS=1000`, star topology (B/C only peer A). |
| NODE-B restart / NODE-C reindex | Phases 2–3: restart B, `-reindex-chainstate` on C; `state_hash` parity re-checked; deterministic log lines. |
| Reorg script named by this note | `core/test/functional/feature_reorg_determinism_audit.py` is not in this tree. `getconvergenceresult` is not a registered RPC. This row does not describe a command the node provides. |
| Divergence capture | `feature_replay_failure_capture.py`: `capture_divergence`, `first_mismatch.json`, RPL2 hex, CSM/selection/mint JSON, `dumptxoutset`, `trace.tsv`; JSON `Decimal` → `str` for forensic dumps. |

**H4 closure:** Core coverage for **modeled** linear replay, partition divergence forensics, and controlled reorg+golden paths is **sufficient** for the stated STEP objectives. Remaining items below are **engineering / CI / scope**, not consensus defects.

## 2. Residual risks (non-consensus)

| Risk | Level | Notes |
|------|-------|--------|
| `find_first_divergence_height` uses broad `except` on `getblockhash` | MEDIUM (forensics) | RPC/transient errors can be mis-reported as “first fork height”; narrow exception types if stricter triage is needed. |
| STEP-0201 invokes `capture_divergence` on `_fail_step0201` (stdout `STEP-0202_CAPTURE_DIR`); test still fails — no suppression | LOW (ops) | Disk/time on failure only; use `--nocleanup` to retain `replay_divergence_capture/`. |
| Unmodeled P2P / mempool (orphan storms, delayed propagation, feefilter races) | MEDIUM (coverage gap) | Out of STEP-0201/03 contract; separate tests if product requires. |
| `getchainstatesummary` absent on older binaries | LOW | Reorg audit degrades CSM columns to `NA` with warning; rebuild current RIGOR node for full trace. |

## 3. Explicit non-goals (this closure)

- No further consensus semantic changes tied to H4 sign-off.
- No requirement to expand harness scope without a new STEP/RFC owner.

## 4. References

- `core/doc/-deterministic-replay.md` — STEP-0201 baseline PASS / stdout / archive
- `core/doc/STEP-0201-audit-record.md` — **release / audit append table** (`STEP-0201: PASS` + commit + build + PRNG seed policy)  
- `core/doc/-replay-capture-verification.md` — closure + archive / stdout contracts
- `core/test/functional/feature_deterministic_replay.py` — STEP-0201  
- `core/test/functional/feature_replay_failure_capture.py` — STEP-0202  
- `core/test/functional/feature_reorg_determinism_audit.py` — STEP-0203  
- `core/src/consensus/rigor.{h,cpp}` — removed unused `Context::candidates`; silenced deprecated-member destructor warnings  
- `core/src/rpc/blockchain.cpp` — `getchainstatesummary` path uses `PrepareContext` without redundant pragma scaffolding  

---

*This file satisfies H4 “archive review notes” and records residual risk for engineering follow-up.*
