# RIGOR Explorer / Indexer Architecture

Mainnet genesis
`e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468`,
HRP `rg`. Explorer is a derived view of `rigord` RPC.

**Document status:** Production documentation  
**Mode:** INDEXER / EXPLORER / READ-ONLY CHAIN INTERPRETATION  
**Historical protocol baseline (archived):** commit `4437597197aa713396a123673cc36606346ad523`, tag `mainnet-freeze-rfc014-2026-05-25`  
**Historical scope:** Closed testnet foundation — not the current production identity.

```
equivalence_claimed = false
explorer_state_authority = false
indexer_consensus_authority = false
```

```
Consensus defines reality.
Explorer displays reality.
Database stores derived representations.
```

The indexer is a transformation layer only. API output is a derived view
(`API_Response = Derived_View(consensus_data)`). Identity:
[`../whitepaper/RIGOR_PROTOCOL_IDENTITY.md`](../whitepaper/RIGOR_PROTOCOL_IDENTITY.md).
A rebuilt index starts from genesis using the current classifier.

---

## 1. Purpose

Provide a **protocol-correct**, **read-only** indexing and visibility layer for the RIGOR **mainnet** (and operator-selected test chains). The stack MUST mirror node-reported state; it MUST NOT implement alternate consensus rules, probability models, or a second asset classifier.

**In scope:** ORU, RES, convergence (`OP_CONVERGE`), convergence lineage, replay-field display, witness-v1 RES spend interpretation.  
**Out of scope:** Production UI polish, exchange APIs, analytics dashboards, account models, NFT/token layers.

---

## 2. Design principles

| Principle | Explorer obligation |
|-----------|---------------------|
| Script-as-compliance | Classify ORU/RES from `scriptPubKey` / RPC decode — never from a parallel `asset` column in UTXO storage; explorer display is not consensus truth |
| Node is truth | All indexed facts derivable from STABLE RPC + frozen decode rules |
| txid identity | Convergence / mint txs keyed by **txid** |
| No consensus in API | REST/API layer is presentation + query only |
| Deterministic ordering | API sorts by `(height ASC, txid ASC)` — never hash-map iteration order |

---

## 3. Indexer topology

```
┌──────────────────────────────────────────────────────────────────┐
│  Operator browser (VPN / SSH tunnel during closed phase)          │
└────────────────────────────┬─────────────────────────────────────┘
                             │ HTTPS (optional static UI)
┌────────────────────────────▼─────────────────────────────────────┐
│  Explorer UI (minimal pages — see EXPLORER_UI_MINIMUM_SPEC.md)   │
└────────────────────────────┬─────────────────────────────────────┘
                             │ REST (read-only)
┌────────────────────────────▼─────────────────────────────────────┐
│  Indexer API service                                              │
│  - block importer                                                   │
│  - tx decoder cache                                                 │
│  - convergence rows from block transactions and getblockconvergence │
│  - mint rows from body transactions that destroy ORU and create RES │
└────────────────────────────┬─────────────────────────────────────┘
                             │ JSON-RPC 1.1 (loopback)
┌────────────────────────────▼─────────────────────────────────────┐
│  rigord                                                            │
│  txindex=1, no wallet, rpcbind=127.0.0.1                            │
└────────────────────────────────────────────────────────────────────┘
```

### 3.1 Component responsibilities

| Component | Responsibility | MUST NOT |
|-----------|----------------|----------|
| **rigord** | Full validation, chainstate, STABLE RPC | Be replaced by indexer for validity |
| **Importer** | Persist blocks/headers/txs from RPC | Recompute mint arithmetic as a second authority |
| **Enricher** | Attach `0xbd` destroys and body-tx RES mints from the block | Treat a draw, a winner, or a missing RPC as issuance |
| **API** | Serve sorted, labeled ORU/RES views | Expose `invalidateblock` or wallet keys |
| **UI** | Display indexed fields + disclaimers | Merge ORU+RES balances |

### 3.2 Deployment placement (closed testnet)

Per closed topology: **NODE-C** hosts `rigord` + indexer + API. NODE-A holds spendable keys; NODE-B mines. Explorer MUST NOT share a process with NODE-A wallet.

---

## 4. RPC ingestion model

### 4.1 Primary ingestion paths

| Phase | RPC methods this explorer calls | Notes |
|-------|-------------|-------|
| **Initial sync** | `getblockhash`, `getblock` (verbosity 2) | `explorer/src/importer.ts` |
| **Block body** | `getblock` | Destroy outputs are read from the block, not from a separate convergence RPC |
| **Tip** | `getblockhash`, `getblock` | Hash change at the same height is the reorg signal in the importer |
| **Tx detail** | `getrawtransaction` (verbose true) | The call passes `true` |
| **Chain status** | `getblockchaininfo`, `getnetworkinfo`, `getconvergencestatus`, `getruntimesupply`, `getrawmempool` | `getrawmempool` is called with no arguments. Its `verbose` argument defaults to false |

### 4.2 Forbidden ingestion

- `getconvergeinfo` is not a registered RPC
- Retired fields: `rigor_target`, `res_draw_target`, `res_target_ctx`
- Nonexistent RPC: `getconvergencedrawcandidates`
- Wallet RPC on indexer node (no wallet loaded)

### 4.3 Decode path

Use `decoderawtransaction` / verbose `getrawtransaction` for script and witness display. Asset labels MUST follow the same rules as `AssetTypeFromScriptPubKey` (witness v0 → ORU; `OP_CONVERGE` → ORU destroy; other v1 32-byte → RES). Historical RFC-014 Witness v1 `CVG2` is retired and must not be labeled as a live asset.

---

## 5. Polling vs ZMQ

| Mechanism | Recommendation (closed testnet) | Rationale |
|-----------|----------------------------------|-----------|
| **HTTP polling** | **Default** — 30 s tip poll + immediate import on hash change | Simple, auditable, no extra ports; sufficient for 60 s block spacing |
| **ZMQ** | **Optional** later (`hashblock`, `hashtx`) | Reduces latency; requires `zmqpub*` config and firewall rules; not required for foundation |

**Foundation rule:** Implement correctness on polling first. ZMQ is an optimization only if it does not change import ordering or introduce race-driven nondeterminism in the API (serialize tip updates behind a single importer mutex).

---

## 6. Database architecture

### 6.1 Logical schemas

| Store | Contents | Primary keys |
|-------|----------|--------------|
| `blocks` | Header fields + Rigor extensions | `block_hash` |
| `transactions` | Raw hex, txid, wtxid, block link | `txid` |
| `outputs` | Per-vout script, value, inferred asset | `(txid, vout)` |
| `convergence_events` | ORU convergence transactions (`0xbd` destroy; the same transaction may mint RES) | `(block_hash, convergence_txid)` |
| `res_mints` | RFC-020 body-tx mint: same txid destroys ORU and creates RES | `(mint_txid, mint_vout_index)` |
| `chain_meta` | Importer tip, last good height | singleton |

### 6.2 Engine choice (summary)

| Engine | Closed testnet | Public testnet |
|--------|----------------|----------------|
| **SQLite** | Acceptable (< ~50k blocks) | Risk under concurrent API load |
| **PostgreSQL** | Recommended if MON-1 + API co-located | Preferred for HA read replicas |

See `CLOSED_TESTNET_EXPLORER_DEPLOYMENT.md` for sizing.

### 6.3 Immutability

Blocks at height `h` are immutable until a reorg. On a reorg, delete rows above the fork and import the new chain from the node.

### 6.4 Pre-mainnet database reset

Development databases created before script alignment may contain retired
classifications. Public network launch will rebuild the explorer view and
indexed observations from genesis using current consensus classification
rules.

`OP_CONVERGE` (`0xbd`) is the Convergence classification. Fresh indexing starts from genesis.

---

## 7. txid vs wtxid policy

| Use case | Identifier | Rule |
|----------|------------|------|
| Primary keys and URLs | **txid** | Mandatory |
| Convergence transaction and its RES outputs | **txid** | The mint, when present, is that body transaction under the RFC-020 inequality |
| Mempool display (if added) | wtxid optional secondary | Label as “witness id”; never primary |
| Foreign keys / JOINs | **txid** only | wtxid display-only column |

Indexed RES mints are body transactions in the block, not a winner list.

---

## 8. Reorg handling model (summary)

| Parameter | Value / policy |
|-----------|----------------|
| Detection | `getbestblockhash` change, or the same height with a different hash |
| Action | Truncate the index above the fork and re-import from `getblock` / `getblockconvergence` |

---

## 9. Cross-references

| Document | Topic |
|----------|-------|
| `EXPLORER_RPC_COMPATIBILITY_MATRIX.md` | RPC tiers |
| `RES_INDEXING_RULES.md` | RES/ORU/convergence indexing |
| `WITNESS_V1_DISPLAY_RULES.md` | Witness v1 display |
| `EXPLORER_SECURITY_BOUNDARIES.md` | Forbidden behaviors |

---

## 10. Document metadata

| Field | Value |
|-------|--------|
| Risk classification | **MEDIUM** (mis-indexing); **HIGH** if wtxid used as identity |
| Explorer classification | **FOUNDATION-ARCHITECTURE** |
| Consensus impact | **NONE** |
| Replay impact | **INDIRECT** — wrong display mimics consensus failure |
| Determinism impact | **INDIRECT** — API must use explicit sorts |
| Indexing risk | **MEDIUM** |
| Minimal safe recommendation | Follow the node tip; index `getblock` and `getblockconvergence`; txid keys |
