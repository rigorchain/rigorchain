# RIGOR Public Site — Route and Data Binding Specification

**Task:** ``
**Document:** Frontend data sources, routing, and UI state machine  
**Parent:** [SITE_ARCHITECTURE.md](./SITE_ARCHITECTURE.md)

| Field | Value |
|-------|-------|
| `equivalence_claimed` | **false** (must display from live sources when available) |

---

## 1. Data binding principles

| Rule | Requirement |
|------|-------------|
| **NEVER use mock data** | No placeholder blocks, lorem ipsum heights, or synthetic tx lists |
| **ALL failures visible** | HTTP/RPC errors surfaced in UI — no `catch {}` silence |
| **Degraded state explicit** | Per-module amber when partial data |
| **No silent fallback** | If secondary source fails, show failure — do not substitute primary with fake values |
| **rigord is authority** | RPC wins on conflict with explorer |
| **Traceability** | Every live value: `{ value, source, fetched_at, error? }` |

---

## 2. Environment configuration

| Variable | Purpose | Example |
|----------|---------|---------|
| `VITE_RPC_PROXY_URL` | Read-only JSON-RPC POST endpoint (operator proxy to `rigord`) | `http://127.0.0.1:18761` |
| `VITE_EXPLORER_API_URL` | Explorer indexer base | `http://127.0.0.1:8080` |
| `VITE_REFRESH_INTERVAL_MS` | Auto-refresh default | `30000` |
| `VITE_PROOF034_STATUS_URL` | Optional published ops JSON (no mock if unset) | `/.runtime/ops-metrics/...` |

If env vars unset, site runs in **static-only mode** with global `OFFLINE` badge — not fake live data.

---

## 3. Protocol data sources

### 3.1 Primary: rigord JSON-RPC

Transport: HTTP POST `VITE_RPC_PROXY_URL` with JSON-RPC 2.0 body.

| Method | Used on | Required | Soft-fail |
|--------|---------|----------|-----------|
| `getblockchaininfo` | All pages (status bar), `/explorer` Chain tab | **YES** | — |
| `getnetworkinfo` | `/explorer` Chain, `/system-status` | **YES** | — |
| `getconvergencestatus` | `/explorer` Convergence, status bar | **YES** | — |
| `getblockconvergence` | `/explorer` Convergence (block-scoped cumulative destroy) | NO | **YES** — show the block's `0xbd` outputs from `getblock` if this call fails |

**Forbidden RPC methods in public site client:**

`sendtoaddress`, `sendrawtransaction`, `submitblock`, `generatetoaddress`, `signrawtransaction*`, `wallet*`

### 3.2 Secondary: explorer API enrichment

Base: `VITE_EXPLORER_API_URL`

| Endpoint | Method | Used on | Required |
|----------|--------|---------|----------|
| `/api/status` | GET | Global status, equivalence badge | **YES** for explorer health |
| `/api/res/mints` | GET | `/explorer` RES tab | NO — degraded if 404/5xx |
| `/api/block/:hash` | GET | `/explorer` Block tab | NO — per-request error |
| `/api/lineage/:txid` | GET | `/explorer` Lineage tab | NO |
| `/api/tx/:id` | GET | `/explorer` Transaction tab | NO |
| `/healthz` | GET | `/system-status` | Optional probe |

### 3.3 Static publication content

| Route | Build-time import |
|-------|-------------------|
| `/protocol` | `content/protocol.md` ← unified whitepaper §1 |
| `/axioms` | `content/axioms.md` ← unified §2 + AXIOMS_v1.0 |
| `/lab` | `content/lab.md` ← unified §3 |
| `/verification` | `content/verification.md` ← unified §3.2 + PROOF-034 summary |

Static content does not change when RPC is offline.

---

## 4. Binding map by route

| Route | Static content | Live bindings |
|-------|----------------|---------------|
| `/` | Hero, pillars | `getblockchaininfo`, `/api/status` |
| `/protocol` | ProtocolViewer tree | `getblockchaininfo` (strip only) |
| `/axioms` | AxiomViewer | None required |
| `/lab` | LabViewer | None required |
| `/explorer` | Disclaimers only | Full RPC + explorer set |
| `/verification` | PROOF-034 copy | Optional `VITE_PROOF034_STATUS_URL` |
| `/system-status` | Gate table (static) | All probes |

---

## 5. Global UI state machine

### 5.1 Aggregate states

```text
                    ┌─────────────┐
         RPC fail   │   OFFLINE   │
        ──────────► │ (no live)   │
                    └─────────────┘
                           ▲
                           │
┌──────────┐    partial    │    RPC ok + explorer fail
│ SYNCING  │◄── IBD=true ──┼──────────────────────────┐
└──────────┘               │                          │
       │                   │                          ▼
       │                   │                 ┌─────────────┐
       └───────────────────┼────────────────►│  DEGRADED   │
                           │                 └─────────────┘
                           │                          ▲
                           │     explorer ok,         │
                           │     optional RPC soft-fail│
                           ▼                          │
                    ┌─────────────┐                   │
                    │   ONLINE    │───────────────────┘
                    │ (full live) │
                    └─────────────┘
```

### 5.2 State definitions

| State | Condition |
|-------|-----------|
| **OFFLINE** | `getblockchaininfo` unreachable OR RPC proxy unset |
| **SYNCING** | RPC OK and `getblockchaininfo.initialblockdownload === true` |
| **DEGRADED** | RPC OK but explorer down, or `getblockconvergence` unavailable on the convergence tab |
| **ONLINE** | RPC OK, not IBD (or IBD acknowledged in UI), explorer `/api/status` 200 |

**Priority (highest wins for badge):** OFFLINE > SYNCING > DEGRADED > ONLINE

### 5.3 `equivalence_claimed` handling

```text
IF /api/status.equivalence_claimed === true
  → FAULT BANNER (red) + log incident
  → Treat global state as DEGRADED minimum
ELSE
  → Display badge "equivalence_claimed: false" (normal)
```

Static site assumes `false`; live check overrides display only.

---

## 6. Per-page state machine

Every page with live data implements:

```text
loading → live | error | degraded
```

| Substate | UI |
|----------|-----|
| **loading** | Skeleton without numeric placeholders |
| **live** | Values + `SourceLabel` |
| **error** | `ErrorPanel` + last `fetched_at` |
| **degraded** | Amber banner + partial fields |

Pages without live bindings (`/axioms`, `/lab` static-only) always show `static` source tag on publication content.

### 6.1 Explorer module — per-tab isolation

Each explorer tab maintains **independent** state:

```typescript
// Conceptual — implementation TBD
type TabState = {
  status: 'idle' | 'loading' | 'live' | 'error' | 'degraded';
  data: unknown | null;
  error: string | null;
  fetched_at: string | null;
};
```

**Rule:** RPC failure on Convergence tab **must not** clear Chain tab data. No global failure wipe.

### 6.2 Explorer sync model

| Mechanism | Spec |
|-----------|------|
| **Auto-refresh** | `setInterval(30_000)` per active explorer tab |
| **Manual refresh** | Toolbar button; aborts in-flight fetch, sets `loading` |
| **Stale indicator** | If `now - fetched_at > 2 * interval`, show amber "stale" |
| **RPC failure isolation** | Failed tab → `error`; other tabs unchanged |
| **Concurrent requests** | Cancel prior fetch on new refresh (AbortController) |

---

## 7. Request adapters

### 7.1 RPC adapter

```text
rpcCall(method, params?) → { result?, error?, latency_ms, fetched_at }
```

- Timeout: 5s (align with `probe_live_stack.sh`)
- On error: return `error` string; UI never invents `result`

### 7.2 Explorer adapter

```text
explorerGet(path) → { body?, status, error?, fetched_at }
```

- Timeout: 10s
- Non-2xx → `error` state; no cached fake body

### 7.3 Conflict resolution

When RPC and explorer disagree on shared fields (e.g. height):

```text
display.height = rpc.blocks
IF explorer.height !== rpc.blocks
  show MismatchBanner("explorer height ≠ rpc — displaying rigord value")
```

---

## 8. Field-level binding tables

### 8.1 System status bar

| UI field | RPC / API path |
|----------|----------------|
| `chain_height` | `getblockchaininfo.blocks` |
| `best_hash` | `getblockchaininfo.bestblockhash` |
| `ibd` | `getblockchaininfo.initialblockdownload` |
| `peer_count` | `getnetworkinfo.connections` |
| `convergence_summary` | `getconvergencestatus` (first-level keys) |
| `equivalence_claimed` | `/api/status.equivalence_claimed` (fallback: static false + `source: static`) |
| `explorer_ok` | `/api/status` HTTP 200 |

### 8.2 Explorer — Chain tab

| UI field | Source |
|----------|--------|
| Height, chain, difficulty | RPC `getblockchaininfo` |
| Connections | RPC `getnetworkinfo` |

### 8.3 Explorer — Convergence tab

| UI field | Source |
|----------|--------|
| Status fields | RPC `getconvergencestatus` |
| Destroy cost, tip, cumulative RES, cumulative ORU destroyed | RPC `getconvergencestatus` |
| Cumulative destroyed ORU through a block | RPC `getblockconvergence` |

### 8.4 Explorer — RES tab

| UI field | Source |
|----------|--------|
| Mint list | Explorer `GET /api/res/mints` |
| Tip mint cost | RPC `getconvergencestatus` (`destroy_cost`, `min_whole_res_burn`) |

### 8.5 Explorer — Block / Tx / Lineage

| UI field | Source |
|----------|--------|
| Block body | `GET /api/block/:hash` |
| Transaction | `GET /api/tx/:id` |
| Lineage graph | `GET /api/lineage/:txid` |

User supplies hash/txid via search input; empty search → empty state, not demo block.

---

## 9. Error copy (standard)

| Code | User-visible message |
|------|---------------------|
| `RPC_OFFLINE` | rigord RPC unreachable. Live chain data unavailable. |
| `EXPLORER_OFFLINE` | Explorer API unreachable. RPC-only mode. |
| `PARTIAL_RPC` | Node responded but required field missing. |
| `MISMATCH` | Explorer and rigord disagree. Displaying rigord value. |
| `NO_MOCK` | (internal) Never shown — prevent mock injection in dev |

---

## 10. Routing implementation notes

| Path | SPA behavior |
|------|--------------|
| All routes | Client-side router (e.g. React Router) with static hosting fallback to `index.html` |
| Deep links | `/explorer?tab=block&hash=…` — query params for explorer context |
| 404 | Custom page: link home + "route not in publication site" |

---

## 11. Acceptance checklist

- [ ] No mock data paths in adapters
- [ ] Each explorer tab isolated state
- [ ] 30s refresh + manual trigger
- [ ] OFFLINE/DEGRADED/ONLINE/SYNCING derivable from probes
- [ ] `equivalence_claimed` from `/api/status` when available
- [ ] Forbidden wallet/mining RPC not callable

---

## 12. References

- [SITE_ARCHITECTURE.md](./SITE_ARCHITECTURE.md)
- [UI_LAYOUT_SPEC.md](./UI_LAYOUT_SPEC.md)
- [explorer-indexer-mvp](../../explorer) (existing API)
- `contrib/testing/ops/probe_live_stack.sh`
