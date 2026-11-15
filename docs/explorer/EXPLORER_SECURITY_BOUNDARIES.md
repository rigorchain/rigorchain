# Explorer Security Boundaries

**Document status:** Production documentation  
**Principle:** Explorer is **non-authoritative** for consensus and replay.

---

## 1. Explicit prohibitions

The explorer/indexer stack MUST NOT:

| ID | Forbidden behavior |
|----|-------------------|
| **F-01** | Derive consensus validity from explorer DB cache (“indexed therefore valid”) |
| **F-02** | Use **wtxid** as primary identity for convergence, convergence lineage, or URLs |
| **F-03** | Apply hidden indexing heuristics (probability inference, ML eligibility, fee guessing) |
| **F-04** | Auto-generate replay assumptions not present in RPC/witness (invented `bind_hash`, wrong `target_prev` as T) |
| **F-05** | Reimplement the mint inequality as an explorer-local authority instead of displaying the node's accepted transactions |
| **F-06** | Parse `rigor_target`, `res_draw_target`, or `bits` as RES inequality |
| **F-07** | Call `invalidateblock` / `reconsiderblock` from API |
| **F-08** | Hold spendable wallet keys on indexer host |
| **F-09** | Expose `rigord` RPC to `0.0.0.0/0` |
| **F-10** | Merge ORU+RES into account balances |
| **F-11** | Treat mempool as finalized RES mint |
| **F-12** | Use Taproot/BIP341 verification or labeling |
| **F-13** | Treat P2WSH, P2PKH, or a Bitcoin Script redeem script as a spendable RIGOR output |
| **F-14** | Keep a pre-reorg Convergence or RES display after the node tip has moved, without truncating and re-importing |

---

## 2. Trust model

```
Consensus truth:  rigord validation + chainstate (NODE-C)
Explorer truth:   copy of RPC outputs at import time
User trust:       MUST verify critical claims via own node RPC
```

**Closed testnet:** Operators may trust NODE-C; **public** phase requires client-side verify for high-value audits.

---

## 3. Cache semantics

| Cache type | Allowed | Invalidation |
|------------|---------|--------------|
| Block body hex | Yes | Reorg truncate |
| `getblock` / `getblockconvergence` snapshots | Yes | Reorg + re-fetch |
| Derived lineage edges | Yes | Reorg truncate |
| “Eligibility score” computed locally | **No** as authority | — |

---

## 4. API hardening (closed phase)

| Control | Setting |
|---------|---------|
| Authentication | VPN/mTLS — no anonymous public write |
| Rate limit | 60 req/min/IP on API |
| CORS | Deny `*` in closed phase |
| Admin routes | None on explorer |

---

## 5. Data exposure

| Data | Exposure |
|------|----------|
| Testnet addresses | Public within VPN |
| RPC credentials | Never in UI bundle |
| `debug.log` | Ops only |

---

## 6. Incident response

| Event | Action |
|-------|--------|
| Root mismatch | Disable API; keep node running; file SEV-1 |
| Suspected indexer bug | Compare `getblock` and `getconvergencestatus` with the index |
| Chain split | Follow the node tip. The indexer does not choose a fork |

---

## 7. Document metadata

| Field | Value |
|-------|--------|
| Risk classification | **HIGH** if F-01–F-04 violated |
| Explorer classification | **SECURITY-BOUNDARY** |
| Consensus impact | **NONE** (if boundaries held) |
| Replay impact | **INDIRECT** — violations break audit trust |
| Determinism impact | **INDIRECT** |
| Indexing risk | **CRITICAL** on heuristic indexing |
| Minimal safe recommendation | RPC-authoritative; txid keys; no hidden logic |
