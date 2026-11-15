# RFC-020 Adoption Status

**Date:** 2026-09-12  
**Decision:** Adopt RFC-020 as the next (and intended final) ORU/RES issuance
rule.  
**Shipping C++:** RFC-020 in `res_issuance.h` and `GetBlockSubsidy`. Params no
longer carry a halving schedule. Observational ORU cumulative is the closed
form `height · 128`. Live Coq issuance models match. Explorer / website
issuance pages / SPEC-002 / SPEC-003 tables now describe RFC-020.

```
equivalence_claimed = false
documentation_consensus_authority = false
refinement_proved = false
implemented = true
```

RFC-020 is the decided successor to RFC-019 and to the ORU three-halving
schedule. The first consensus tranche is in `core/src/`: the mint predicate
and the ORU subsidy. See [`MAINNET_PROTOCOL_FREEZE_DECLARATION.md`](../protocol/MAINNET_PROTOCOL_FREEZE_DECLARATION.md).

**Numeric witness:** `tools/res_continuous_price_schedule.py`.
The C++ realization is `core/src/consensus/res_issuance.h`.

---

## 1. Two authorities (do not collapse them)

| Layer | Authority | RES cap | ORU subsidy `h ≥ 1` | Price |
|-------|-----------|---------|---------------------|--------|
| **Shipping issuance** | RFC-020 + `res_issuance.h` / `GetBlockSubsidy` | `419 430 400 000 000` atoms = `2²²` RES | 128 forever | `R · (2²⁵+h)² ≤ B << 40` |
| **Still leftover** | Audit snapshots / lottery-era Python oracles | old text in places | — | — |

Unchanged in both: `RES_COIN = 1e8`, genesis destroy cost **1024 ORU / RES**, genesis
block subsidy **0**, mint locus (body-tx `OP_CONVERGE` destroy MAY mint Witness v1
RES), Ed25519, OpuHash, RFC-018, maturity 100.

If a document disagrees with RFC-020 about the issuance rule, RFC-020 is
correct. If it disagrees with `core/src/` about *what the node does today*,
that is an implementation description — not a silent protocol change.

---

## 2. What this documentation pass does and does not do

**Done in this pass**

- Lift the RFC-019-specific freeze (pole 8, epoch 210 000, last epoch 250,
  derived 019 cap, ORU halvings) for the purpose of implementing RFC-020.
- Record RFC-020 as the decided successor.
- Banner SPEC-002 / SPEC-003 / the economic-interaction note so they do not
  look like they are unaware of the decision.

**Still not done**

- Audit snapshots (`docs/audit/`, `docs/security/RES_MONETARY_*`) that still
  quote RFC-019 numbers — historical snapshots, not rewritten in this pass.
- `refinement_proved` stays **false**. C++ ↔ Python SSOT differential now pins
  RFC-020; that is engineering correspondence, not a ConnectBlock proof.

---

## 3. Decided parameters (RFC-020)

| Name | Value |
|------|-------|
| `ORU_FLAT_BLOCK_SUBSIDY` | 128 ORU (`2⁷`) for every `h ≥ 1` |
| `ORU_GENESIS_BLOCK_SUBSIDY` | 0 (unchanged) |
| `RES_DESTROY_COST_GENESIS` | 1024 ORU (`2¹⁰`, unchanged) |
| `RES_DESTROY_POLE` | `2²⁵` blocks |
| `RES_DESTROY_SHIFT` | 40 (forced by the genesis destroy cost) |
| `RES_MAX_SUPPLY` | `2²²` RES = `419 430 400 000 000` atoms |
| Mint predicate | `R > 0 → R · (2²⁵+h)² ≤ B << 40`; `R = 0` unconstrained |

Identity: `cap = (S/P0)·H = 2⁷/2¹⁰·2²⁵ = 2²²`.

---

## 4. Next implementation tranche

**Landed:** `res_issuance.h`, `GetBlockSubsidy`, lock version 3,
`RES_MAX_SUPPLY = 2²²`, and the C++ unit tests that pin those two functions.

**Landed this tranche:** live Coq issuance rewrite.

- `ResSchedule.v`: continuous `destroy_cost(h) = P0 · (2²⁵+h)² >> 50`, cap `2²²` RES,
  mint predicate `R · (2²⁵+h)² ≤ B << 40`.
- `IssuanceHeightBoundary.v`: flat 128 ORU for `h ≥ 1`; RES is height-continuous.
- `CanonicalIssuance.v`: cumulative ORU = `n · 128` ORU; `res_issued_at_height = 0`.
- `ConsensusKernelRefinement.v`: `cpp_ResDestroyCostAtHeight` is the continuous destroy cost;
  `issuance_refinement` holds; `refinement_proved` stays **false**.
- FeeBurn cap pins moved to `419430400000000`.

**Landed this documentation tranche:** explorer / website schedule code,
SPEC-002 / SPEC-003, public `WHITEPAPER.md`, chapters 02/04/05/06/08/09,
long-form protocol / identity / formal-verification papers, and website
public pages (Landing / About / Mining / Wallet / Whitepaper / ProtocolCard /
ConvergencePanel). Leftover `RATE(k)` / 125 / lottery language in those
files is marked historical.

**Landed this kernel tranche:** `rigor_kernel/Supply/SupplySchedule.v` is
RFC-020 (flat 128 ORU, continuous `destroy_cost(h)`, pole mint predicate, cap
`2²²` RES). Dependents (`RESIssuance`, `RESValidator`, `SupplyTransition`)
use `R · (2²⁵+h)² ≤ B << 40`. Python spec issuance +
`test_rfc017_schedule_parity.py` now pin RFC-020 (filename is historical).

**Landed this oracle tranche:** `rfc018_fee_burn.py` / golden JSON / parity
test pin `RES_MAX_SUPPLY = 419430400000000` and eight-decimal `RES_COIN`.
`diff_oracle.py` shadows RFC-020 body-tx `destroy_cost(h)`.

**Landed this extraction tranche:** faithful `extracted/Extract.v` re-extract
of the six validators. `check_res` is RFC-020 body-tx + pole predicate.
`extract_and_test.sh` and `diff_verify.sh` both PASS. Extracted lottery
`supply_cli` / `extraction/ExtractedKernel.ml` (native-int leftover) are
still not the faithful path.

**Landed this harness tranche:** `generate_phase3_vectors.py` /
`phase3_golden_vectors.json` / `NegativeCoreHarness.v` /
`GoldenHarness.v` now pin RFC-020. Cap is `419430400000000`. There is
no `RATE=0` / unauthorized-height mint: observational k=251 still has
`destroy_cost(h)>0`. `NEG-002` is the genesis-exact 1024-ORU destroy failing at
height 1. Golden lock stays 433/226. Coq `COQ=1` compile of these
harnesses still Requires archived `ReplayReorgCore` (not this tranche).

**Landed this supply-cli tranche:** `MintSupplyConservation.v` dropped
`WindowGeneric`. `apply_delta` no longer treats `destroy_cost(h)` as a per-block
RES grant; an authorized mint is any `R ≤ RES_MAX_SUPPLY`.
`extracted/supply_kernel.ml` / `supply_shadow_diff.py` pin 128 ORU and
1 RES (`RES_COIN = 1e8`). This is a conservation slice, not the pole
predicate. `sighash_kernel.ml` was not rewritten.

**Landed this live-docs tranche:** the freeze declaration, README,
`SCRIPT_STRUCTURE`, opcode spec, `SECURITY.md`, wallet design lock,
and FormalizationFrontier now describe shipping RFC-020.

**Landed this explorer/wallet-spec tranche:** explorer UI / indexing /
witness display and wallet UX / journey / first-run specs now say
`destroy_cost(h)` and cap `2²²` RES. Audit / `docs/security` P0 snapshots
are still historical records.

**Landed this RPC / wallet-copy tranche:** `res_mint_unit_cost` /
`getconvergencestatus` / `createconvergence` help and errors no longer
say `RATE(k)` or “issuance has ended”. Wallet/Qt lock headers and
first-run copy take `destroy_cost(h)` from the node. Functional mint tests
quote the live height instead of a 251-epoch cutoff. Audit /
`docs/security` P0 snapshots are still historical records.

**Landed this mint-construction tranche:** `res_mint_unit_cost` /
`getconvergencestatus` expose `min_whole_res_burn`. Display `rate`
must not be fed back as B; the pole inverse is the construction
destroy. Functional mint tests pin that split. Qt market panel no longer
says the calendar bucket is a mint horizon.

**Landed this snapshot-banner tranche:** files that still said
“Live RES issuance is RFC-019” now open with an RFC-020 shipping
notice. `tools/res_issuance_schedule.py` computes an epoch table and a 125 ORU tail. That file is not `GetBlockSubsidy`.

**Landed this body-rewrite tranche:** CODE_SSOT, economic validation,
monetary-policy exec, SPEC-000/001/005/006 live mint/cap rows,
shipping-parameter sync, architecture profile, and the remaining
live parameter tables now state RFC-020 (`2²²` RES, pole predicate,
flat 128 ORU). Historical banners still quote the RFC-019 numbers
they superseded. Incident write-ups keep observed facts.
`refinement_proved` stays **false**.

**Landed this formal-rewrite tranche:** `ResSchedule.v`, `SupplySchedule.v`,
`CanonicalIssuance.v` and `IssuanceHeightBoundary.v` are algebraic RFC-020
models (pole predicate, closed-form `2²²` cap, flat 128 ORU). Theorem names
match `tools/res_continuous_price_schedule.py`. Twelve issuance golden
JSONs were regenerated from that SSOT. `refinement_proved` stays **false**.

**Landed this economic-sim tranche:** `tools/economic_model_simulator.py`
and `formal_verification/economic_supply_model.json` are described here as
RFC-020 (flat 128 ORU, continuous `destroy_cost(h)`). This tree does not
contain `formal_verification/archive/lottery/` or
`archive/p0-history/docs/formal/`. Production issuance is the predicate in
`core/src/consensus/res_issuance.h`. `refinement_proved` stays **false**.

**Landed this mining-architecture tranche (2026-09-14):** the production
mining path is RFC-020 ORU-only GBT copy-bytes. The node assembler
rejects leftover lottery JSON keys. Coinbase does not insert C / mint /
CVGE.

**Landed this naming pass (2026-09-14):** live Coq, the load-bearing
theorem table, and `FORMAL_THEOREM_INVENTORY.json` (819 names) list
RFC-020 `destroy_cost(h)` / `cap_is_upper_bound` / `predicate_equals_destroy_cost`.
Formal flags stay frozen:
`MATURITY_LEVEL=4.9`, `REFINEMENT_PROVED=false`
(`audit/formal_bridge/FORMAL_STATUS_SOURCE_OF_TRUTH_v1.md`).

**Still open:** some dated P0 bodies still mention old lottery numbers
in historical finding tables. Those rows are being rewritten to RFC-020
and are not issuance SSOT until rewritten. Archive RFC history is
unchanged.

---

## 5. Pointers

| Document | Role after this pass |
|----------|----------------------|
| [`RFC-020`](../protocol/RFC-020.md) | Decided specification |
| RFC-019 | Historical (superseded); not protocol authority |
| [`MAINNET_PROTOCOL_FREEZE_DECLARATION.md`](../protocol/MAINNET_PROTOCOL_FREEZE_DECLARATION.md) | Current freeze locks |
| [`SPEC-002`](../specification/SPEC-002-ECONOMIC-MODEL.md) | Shipping economics; banner points here |
| [`SPEC-003`](../specification/SPEC-003-CONSENSUS-RULES.md) | Shipping consensus rules; banner points here |
