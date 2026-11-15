# Explorer UI Minimum Specification

**Document status:** Production documentation  
**Audience:** Closed testnet operators — not public polish.  
**Baseline:** RFC-020 shipping issuance (continuous `destroy_cost(h)`, body-tx mint).

---

## 1. Global UI requirements

| Requirement | Implementation |
|-------------|----------------|
| Asset labeling | Every amount shows **ORU** or **RES** unit |
| Frozen disclaimer | Footer link to `WITNESS_V1_DISPLAY_RULES.md` bind disclaimer |
| Terminology | No “Taproot” / “BIP341” |
| Identity | Canonical URLs use **txid** |
| Chain badge | “Rigor testnet (closed)” + genesis short hash |
| Error state | Root mismatch banner (red, non-dismissible) |

---

## 2. Page: Blocks (`/blocks`)

| Element | Data source |
|---------|-------------|
| Table columns | height, hash (truncated), time, tx count, ORU subsidy, RES minted, Convergence destroy |
| Row link | `/block/{height}` or `{hash}` |
| Filters | RES minted only (toggle) |
| Pagination | 50 per page, height DESC |

---

## 3. Page: Block detail (`/block/{hash}`)

| Section | Content |
|---------|---------|
| Header | All STABLE header fields including `bits` (labeled PoW), `opus_hash`, RFC-020 `destroy_cost(h)` |
| Convergence panel | `0xbd` destroys in this block, and any Witness v1 RES minted by those transactions |
| Transactions | txid list with badges: coinbase, convergence destroy, res_mint, res_transfer |
| Mismatch alert | If committed ≠ computed (observer integrity only) |

---

## 4. Page: Transactions (`/tx/{txid}`)

| Section | Content |
|---------|---------|
| Header | txid (large), wtxid (small, labeled), block link, confirmations |
| Inputs/outputs | Values with ORU/RES tags |
| Witness panel | For v1 RES: 2-item key-path (the only RES spend path since A-7) |
| Convergence | If `OP_CONVERGE`: destroy amount, Script `OP_CONVERGE`, Asset ORU, State unspendable destroy; same tx MAY mint RES |
| Kind notice | Plain destroy (`R = 0`): **ORU destroyed. No RES minted.** Mint (`R > 0`): **ORU destroyed. RES minted.** Do not use the same caption for both. |
| Mint | If RES mint: RFC-020 body-tx destroy `B` + RES `R` at `destroy_cost(h)` |

**Reject:** `/tx/{wtxid}` routes without redirect to txid.

---

## 5. Page: Addresses (`/address/{bech32}`)

| Element | Notes |
|---------|-------|
| Type detection | v0 ORU vs v1 RES vs invalid |
| UTXO list | From `scantxoutset` / indexer cache |
| History | Tx list spending/creating |
| Balances | Separate ORU and RES — never summed |

---

## 6. Page: ORU balances (`/oru` or dashboard widget)

| Element | Source |
|---------|--------|
| Chain cumulative convergence | `getblockchaininfo.cumulative_convergence` |
| Of which plain destroy | Indexed `OP_CONVERGE` with no RES mint (`R = 0`). Subset of Convergence destroyed — do not subtract from issued or circulating |
| Recent subsidy | Last N coinbase vout[0] |
| Note | “ORU committed to convergence — see convergence lineage” |

Not a wallet — no spend actions.

---

## 7. Page: RES balances (`/res`)

| Element | Source |
|---------|--------|
| Total minted | Sum indexed `res_mints` / chain stats |
| Max supply | `RES_MAX_SUPPLY` (`2²²` RES = `4194304` RES) |
| Current price | `destroy_cost(h)` at tip height (protocol destroy cost, not a market quote) |
| Recent mints | Body-tx `OP_CONVERGE` + Witness v1 RES |
| Decimals | 8 dp display (`RES_COIN = 100000000`) |

---

## 8. Page: Convergence lineage (`/convergence` or lineage graph on tx/block)

| Element | Content |
|---------|---------|
| Graph/table | `OP_CONVERGE` txid → same-tx RES mint (RFC-020) |
| Filters | By height range |
| Destroy vs mint | Plain destroy (`R = 0`) vs mint (`R > 0` at `destroy_cost(h)`) |

---

## 9. Page: Convergence activity (`/convergence`)

| Element | Source |
|---------|--------|
| Per-block destroys | Table from `convergence_events` (`OP_CONVERGE`) |
| Mint amount | Indexed `res_mints` for the same txid |
| Retired lottery fields | Do not treat `draw_eligible` / winner txid as issuance |

---

## 10. Page: Chain status (`/status`)

| Element | Source |
|---------|--------|
| Tip height/hash | `getblockchaininfo` |
| Sync status | `initialblockdownload`, verificationprogress |
| Peers | `getnetworkinfo` (optional) |
| Indexer lag | `node_height - indexer_height` |
| Last P1 check | Timestamp + pass/fail |
| Cluster tip agreement | Ops log compare (manual field OK closed phase) |

---

## 11. Non-pages (explicitly out of scope)

- Exchange charts, price tickers  
- Account login / user balances  
- Token/NFT galleries  
- Mempool fee markets (optional later)

---

## 12. Document metadata

| Field | Value |
|-------|--------|
| Risk classification | **LOW** |
| Explorer classification | **UI-MIN-SPEC** |
| Consensus impact | **NONE** |
| Replay impact | **INDIRECT** — wrong labels mislead |
| Determinism impact | **NONE** |
| Indexing risk | **LOW** |
| Minimal safe recommendation | Ship 8 page types; txid URLs; separate ORU/RES |
