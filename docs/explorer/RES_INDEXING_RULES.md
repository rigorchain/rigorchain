# RES Indexing Rules (Explorer Interpretation)

**Document status:** Production documentation  
**Baseline:** RFC-020 shipping issuance  
**Normative:** RFC-020, RFC-008, `core/src/consensus/res_issuance.h`, `CheckResMintArithmetic`.  
Historical RFC-014 / RFC-017 draw fields and RFC-019 `RATE(k)` are **not** live authority.

---

## 1. Purpose

Exact **read-only** rules for how the indexer classifies and stores RES-related chain facts. These rules MUST match node RPC output; the indexer MUST NOT reimplement mint arithmetic as a second authority. Live mint is `CheckResMintArithmetic` in the confirming body transaction.

---

## 2. Asset separation (ORU vs RES)

| Signal | ORU | RES |
|--------|-----|-----|
| Witness version | **0** (P2WPKH/P2WSH) | **1** (32-byte program) |
| `OP_CONVERGE` (`OP_PUSHBYTES_20 \|\| HASH160 \|\| 0xbd`) | Convergence destroy — **ORU**, unspendable | N/A (same tx MAY also create RES) |
| Native v1 program | N/A | **RES** transfer or mint output |
| Historical RFC-014 Witness v1 `CVG2` | **Retired** — not a live asset (indexer classifies `NONE`) | N/A |
| `listunspent` / decode `asset` | `"ORU"` | `"RES"` |
| Display decimals | 8 (base units per `COIN`) | 8 (`RES_COIN = 100_000_000` atoms per 1.00000000 RES) |

**Forbidden:** Single “balance” column summing ORU + RES.

---

## 3. RES mint detection

A transaction is indexed as **RES mint** when ALL hold:

1. It is a **body** transaction at height `H` (not coinbase).
2. It has exactly one `OP_CONVERGE` output with value `B`.
3. It has one or more Witness v1 RES outputs summing to `R > 0`.
4. Node consensus accepted it, so `CheckResMintArithmetic(R, B, H)` held.

**Indexer MUST NOT:**

- Infer mint from probability heuristics, draw winners, or `W(k)`.
- Mark mint from mempool alone.
- Treat coinbase or body `0xbb` as an accepted RES output. `classifyScriptPubKey` still recognizes the template, then returns asset kind INVALID. C++ `AssetTypeFromScriptPubKey` names the same bytes ASSET_RES; that name is classification, not acceptance. A body transaction is `bad-txns-res-mint-noncoinbase`, a RES output below one atom is `bad-txns-res-output-nonpositive`, and a nonzero mainnet coinbase RES amount is `bad-cb-res-amount`. The indexer does not receive a separate acceptance bit, so it does not store the pattern as confirmed RES balance or supply.
- Treat a body-tx PCORB witness as issuance.

### 3.1 Mint amount

| Rule | Value |
|------|-------|
| Consensus mint increment | Any `R` admitted by `R · (2²⁵+H)² ≤ B << 40`. Genesis display `destroy_cost(0) = 1024` ORU per 1 RES. |
| Scale | `RES_COIN = 100000000` |
| Supply cap | `RES_MAX_SUPPLY = 419430400000000` atoms (`2²²` RES) |

---

## 4. ORU vs RES separation in outputs

For each `vout`:

```
IF script is OP_CONVERGE → asset = ORU (unspendable destroy)
ELSE IF script is witness v0 → asset = ORU
ELSE IF witness v1 AND Historical RFC-014 CVG2 prefix → asset = NONE (retired; not a third asset)
ELSE IF witness v1 AND 32-byte RES program → asset = RES
ELSE IF OP_RETURN → asset = NONE (not in supply)
ELSE → asset = NONE (unknown / nonstandard)
```

Match `AssetTypeFromScriptPubKey` semantics in `core/src/primitives/transaction.cpp`.

Development databases created before script alignment may contain retired
classifications. Public network launch will rebuild the explorer view and
indexed observations from genesis using current consensus classification
rules. No production
CVG2 migration is required. Historical CVG2 conversion is not a consensus
concern. The live Convergence output is the 22-byte `0xbd` script.

---

## 5. Convergence / mint (indexer view)

Live mint is the body-tx pole predicate. Indexer stores destroy `B`, minted `R`,
height, and display `destroy_cost(h)`. Lottery fields
(`draw_eligible`, `|F|`, `res_probability_target`, winner txid) are
**retired** and MUST NOT be treated as issuance authority.

```
Accept  ⇔  R = 0  ∨  R · (2²⁵+H)² ≤ B << 40
```

**Forbidden indexer logic:**

- Recompute a draw using `bits` or `SetCompact(target_prev)` as a price.
- Reimplement `RunSelection` / σ / window 32 as authority.
- Display a declared 21,000,000 cap or six-decimal `RES_COIN`.

---

## 6. Convergence tracking (ORU convergence)

### 6.1 What counts as convergence activity

| Event | Detection |
|-------|-----------|
| Convergence tx | Decode: exactly one positive `0xbd` output |
| Amount | That output's ORU value `B` |

### 6.2 Convergence lineage index

Build edges:

1. **Inclusion:** `convergence_txid` → `block_hash` at height `H`.
2. **Destroy:** `OP_CONVERGE` output value `B`.
3. **Mint reference:** Same txid if Witness v1 RES outputs sum to `R > 0`.
4. Retired: F-set / `draw_winner_txid` as issuance keys.

### 6.3 Cumulative convergence

Use `getblockchaininfo.cumulative_convergence` / per-block header field for chain-level ORU convergence counter — do not sum convergence fees locally unless cross-checking.

---

## 7. Replay-safe asset tracing

Fields that MUST be stored for any RES mint display (there is no longer a proof-bearing RES spend: A-7 removed the script path and the `createresclaim` RPC):

| Field | Why |
|-------|-----|
| `txid` | Mint / destroy identity |
| `height` | Confirming height |
| `burn_B` | `OP_CONVERGE` `nValue` |
| `mint_R` | Sum of Witness v1 RES outputs |
| `price_h` | Display `destroy_cost(h)` at that height |

**Not sufficient for replay display:** wtxid, fee rate, mempool time.

### 7.1 txid vs wtxid (RES domain)

| Operation | Identifier |
|-----------|------------|
| Convergence / mint tx | **txid** |
| Mempool relay dedup | wtxid (display only) |

Test reference: `convergence_consensus_tests.cpp` — `txid_vs_wtxid_locked`.

---

## 8. RES indexing edge cases

| Case | Rule |
|------|------|
| Any height `H ≥ 0` | Mint remains authorized; `destroy_cost(h)` is never 0 |
| `R = 0` with a positive destroy | Legal plain destroy — index as destroy, not mint |
| Multiple `OP_CONVERGE` outputs in one tx | Consensus rejects — indexer may show decode error from node |
| Coinbase `0xbb` | Not issuance — do not count toward the cap |
| Local recompute ≠ node accept | Trust ConnectBlock; file bug against node |

---

## 9. Verification hooks (closed testnet)

| ID | Check |
|----|-------|
| P1 | Every indexed mint satisfies `R · (2²⁵+H)² ≤ B << 40` at its height |
| P2 | Display scale is 8 decimals (`RES_COIN = 1e8`). Six-decimal / 21,000,000 copy MUST NOT appear |
| P3 | Cumulative minted atoms ≤ `419430400000000` |

---

## 10. Document metadata

| Field | Value |
|-------|--------|
| Risk classification | **MEDIUM** (edge cases); **CRITICAL** on root mismatch |
| Explorer classification | **INDEXING-RULES** |
| Consensus impact | **NONE** (indexer); **INDIRECT** if rules diverge from node |
| Replay impact | **INDIRECT** — missing bind fields breaks audit |
| Determinism impact | **NONE** if RPC-authoritative |
| Indexing risk | **HIGH** (wtxid, `rigor_target`, local selection) |
| Minimal safe recommendation | RPC-authoritative eligibility; txid keys; store full bind bundle per mint |
