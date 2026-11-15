# RIGOR System Execution Model

**Issuance:** RFC-020  
**Implementation:** `core/src/validation.cpp`, `core/src/consensus/`,
`core/src/script/`, `core/src/node/miner.cpp` (realization)

```
equivalence_claimed = false
documentation_consensus_authority = false
```

Protocol specifications are normative SSOT.
Implementations realize the frozen protocol.

This document describes how production work runs: consensus connection,
operational mining, and observers. Observers never participate in state
transition.

---

## 1. Execution rules

1. **UTXO-only state** — spend-old / create-new. No account table is
   authoritative.
2. **No Script VM** — `ClassifyRigorTxout` then `VerifyRigorPredicate`.
   No parallel asset field on `CTxOut`.
3. **Mint in the body** — `CheckResMintArithmetic` in `tx_verify.cpp`.
   The pool does not mint RES.

---

## 2. Consensus (`ConnectBlock`)

Authoritative path: `core/src/validation.cpp::ConnectBlock`.

```
Header / OpuHash + ASERT
        ↓
view_at_block_entry (UTXO at block entry)
        ↓
Validate coinbase scriptSig:
  <BIP34 height>
        ↓
For each tx:
  CheckTxInputs · VerifyRigorPredicate · RFC-018 destroy share
  body mint: CheckResMintArithmetic(R, B, h)
        ↓
UpdateCoins  (AddCoins skips Convergence; not a UTXO)
        ↓
Supply caches / RES cap / SetBestBlock
```

### 2.1 Transaction classification

Live classification is `ClassifyRigorTxout` /
`AssetTypeFromScriptPubKey`.

| Shape | Meaning | Domain |
|-------|---------|--------|
| Witness v0 / 20-byte | ORU spend / create | ORU |
| `<HASH160> 0xbd` | Convergence destroy | ORU, not a UTXO |
| Witness v1 / 32-byte | RES spend / mint output | RES |
| `0x6a` + pushes, value 0 | NULL_DATA | NONE |

### 2.2 UTXO update

| Output | UTXO |
|--------|------|
| Witness v0 ORU | Created on connect; removed on spend |
| Witness v1 RES | Standard UTXO after maturity 100 on mint outputs |
| Convergence `0xbd` | Never inserted (`EntersUtxoSet` false) |
| NULL_DATA | Never inserted; `nValue` must be 0 |

### 2.3 Reorg

`DisconnectBlock` rolls UTXO via undo data and clears per-block caches
on `CBlockIndex`. No extra-UTXO mint state.

---

## 3. Operational mining

Production mining is **SOLO GBT**. The node assembles the coinbase. The
miner copies bytes and grinds nonce.

```
wallet rg1q
    ↓
rigord  +  rigor-miner   getblocktemplate {"rules":["segwit"]}
    ↓
copy default_coinbase_tx / scriptSig
    ↓
OpuHash grind  →  submitblock  →  ConnectBlock
```

GBT fields the miner must copy (`core/src/rpc/mining.cpp`):

- `default_coinbase_tx`
- `default_coinbase_scriptsig`

Mutable for SOLO: **time** only. `coinbasevalue` is ORU `vout[0]` only.
RES mint is a body transaction, not a GBT coinbase prize.

**OpuHash** = SHA3-256(`"RGRPOW01" || header_80`). CLI: `-a rgrpow01`.

---

## 4. Observers

Observers are read pipelines. They must not sign, send, `generate`, or
`submitblock`.

### 4.1 Explorer

Directory: `explorer/` (package name `rigor-explorer-indexer-mvp`).

```
rigord RPC  →  PostgreSQL  →  REST /api/*
```

Not a ledger of record. Labels must follow
`AssetTypeFromScriptPubKey`.

### 4.2 Website

`web/rigorchain-site` — `VITE_RPC_PROXY` + `VITE_EXPLORER_API`.

Allowlist: `getblockchaininfo`, `getnetworkinfo`,
`getconvergencestatus`, `getblockconvergence`.

---

## 5. Forward path

| Step | Component | Authority |
|------|-----------|-----------|
| 1 | rigord chain + mempool | Consensus |
| 2 | `getblocktemplate` | Consensus |
| 3 | rigor-miner copies GBT bytes, grinds OpuHash | Operational |
| 4 | Optional pool: Stratum share / ORU payout | Operational |
| 5 | `submitblock` → `ConnectBlock` | **Consensus** |
| 6 | Explorer import | Observer |
| 7 | Website display | Observer |

**Consensus boundary:** step 5. Earlier steps are candidate
construction. rigord may reject.

---

## 6. Reverse path

| Step | Component | Authority |
|------|-----------|-----------|
| 1 | Browser / wallet UI | — |
| 2 | Explorer API or RPC proxy | Observer |
| 3 | rigord read RPC | Consensus (read) |
| 4 | Render | Observer |

Indexed data may lag the tip. Live RPC is authoritative for tip height
and convergence status.

---

## 7. Runtime stores

### Consensus (rigord)

| Store | Scope |
|-------|-------|
| UTXO (`CCoinsViewCache`) | Global chain |
| Mempool | Node-local |
| `CBlockIndex` caches | Per block (supply / converge sums) |
| Chain tip | Atomic advance |

### Pool (operational)

Template / job / share caches. Invalidate on tip change. No RES mint
store.

### Observer

PostgreSQL explorer DB. Browser session. Not consensus.

---

## 8. Failure and invariants

| Failure | Recovery |
|---------|----------|
| Invalid `submitblock` | rigord rejects |
| Reorg | `DisconnectBlock` |
| Stale GBT | Miner refetch tip |
| RPC down | Observer degrades |

| ID | Invariant |
|----|-----------|
| EX-01 | ConnectBlock uses `CheckResMintArithmetic` for body mints |
| EX-02 | Convergence outputs never enter UTXO |
| EX-03 | Miner cannot bypass validation |
| EX-04 | Observers never call mutating RPC |
| EX-05 | `equivalence_claimed = false` |

```
Consensus defines reality.
Explorer displays consensus-derived data.
equivalence_claimed = false
```
