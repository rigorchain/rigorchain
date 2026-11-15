# RIGOR Official Site — API Binding Layer

**Task:** ``
**Parent:** [ARCHITECTURE_OVERVIEW.md](./ARCHITECTURE_OVERVIEW.md)  
**Extends:** [../public-site/ROUTE_AND_DATA_BINDING_SPEC.md](../public-site/ROUTE_AND_DATA_BINDING_SPEC.md)

| Field | Value |
|-------|-------|
| `equivalence_claimed` | **false** |

---

## §4 — API binding layer

Strict **read-only** data ingestion for `www.rigorchain.com`. The browser never calls `rigord` directly on operator LAN; production uses a **public read-only RPC proxy** and **public explorer API** endpoints configured at deploy time.

---

## 1. Authority model

| Source | Role | Precedence |
|--------|------|------------|
| **rigord RPC** | Consensus authority | Wins on conflict |
| **Explorer API** | Public enrichment + indexing | Secondary |
| **Static manifest** | PROOF-034 / GV published artifacts | Verification page only |
| **Static MD** | Publication corpus | Protocol/axiom/lab pages |

---

## 2. Primary: rigord JSON-RPC

### Endpoint

```text
POST ${VITE_RPC_PROXY_URL}
Content-Type: application/json
```

Operator deploys TLS-terminated reverse proxy (e.g. `https://rpc.rigorchain.com`) forwarding to internal `rigord` with **read-only** method allowlist.

### Allowed methods

| Method | Pages | Required | Soft-fail |
|--------|-------|----------|-----------|
| `getblockchaininfo` | All live pages | **YES** | — |
| `getnetworkinfo` | `/network`, `/explorer` | **YES** | — |
| `getconvergencestatus` | `/explorer`, header | **YES** | — |
| `getblockconvergence` | `/explorer` Convergence | NO | **YES** |

### Forbidden methods (client blocklist)

```text
sendtoaddress, sendmany, sendrawtransaction, submitblock,
generatetoaddress, signrawtransactionwithkey, signrawtransactionwithwallet,
importprivkey, dumpprivkey, walletpassphrase, createwallet, loadwallet
```

Adapter MUST reject outbound calls to blocklisted methods at compile/runtime.

### RPC adapter contract

```typescript
type RpcResult<T> = {
  result: T | null;
  error: string | null;
  latency_ms: number;
  fetched_at: string; // ISO8601
};

async function rpcCall<T>(method: string, params?: unknown[]): Promise<RpcResult<T>>;
```

- Timeout: **5000ms**
- On error: `result: null`, `error` populated — UI shows banner, **never synthetic result**

---

## 3. Secondary: Explorer API (primary public data layer)

Base: `${VITE_EXPLORER_API_URL}` (e.g. `https://explorer.rigorchain.com`)

| Endpoint | Method | Pages | Notes |
|----------|--------|-------|-------|
| `/api/status` | GET | Global | Must include `equivalence_claimed` |
| `/healthz` | GET | `/network` | Liveness |
| `/api/block/:hash` | GET | `/explorer` | Block + convergence_panel |
| `/api/tx/:id` | GET | `/explorer` | Tx + lineage_graph embed |
| `/api/res/mints` | GET | `/explorer` RES tab | Mint list |
| `/api/lineage/:txid` | GET | `/explorer` | Lineage graph |
| `/api/blocks` | GET | `/explorer` | Optional block list (`?limit=`) |

### Explorer adapter contract

```typescript
type ExplorerResult<T> = {
  body: T | null;
  status: number;
  error: string | null;
  fetched_at: string;
};
```

- Timeout: **10000ms**
- Rate limits: respect 429; show degraded with retry-after if present

---

## 4. Verification manifest (optional)

```text
GET ${VITE_VERIFICATION_MANIFEST_URL}
```

When unset: `/verification` shows static copy + "manifest not published" — **no fabricated progress**.

When set: parse `verification_manifest.json` fields for GV counts, heights, hashes.

---

## 5. RULES (mandatory)

| Rule | Enforcement |
|------|-------------|
| **NEVER use mock data** | No `MOCK_*` env; no dev fixtures in production build |
| **NEVER silently fallback** | Failed fetch → error UI, not last-good masquerading as fresh without stale label |
| **ALL failures visible** | `ErrorBanner` + `last_error` in state |
| **EACH page independently degrades** | Explorer tab isolation; `/network` can be live while RES tab errors |
| **rigord wins conflicts** | `MismatchBanner` when explorer ≠ RPC on shared fields |
| **Traceability** | `SourceLabel` on every live field |

---

## 6. Binding matrix by route

| Route | RPC | Explorer | Static |
|-------|-----|----------|--------|
| `/` | `getblockchaininfo` | `/api/status` | Hero MD |
| `/protocol` | optional strip | — | §1 MD |
| `/axioms` | — | — | Axioms MD |
| `/lab` | — | — | §3 MD |
| `/explorer` | full allowed set | full allowed set | disclaimers |
| `/network` | `getblockchaininfo`, `getnetworkinfo` | `/api/status`, `/healthz` | — |
| `/verification` | — | — | MD + optional manifest |

---

## 7. Polling and refresh

| Context | Interval | Manual |
|---------|----------|--------|
| Global header | 30s | Refresh icon |
| Explorer active tab | 30s | Tab toolbar |
| `/network` | 30s | Yes |
| Static pages | — | — |

Use `AbortController` to cancel superseded requests.

---

## 8. Production CORS model

```text
Browser (www.rigorchain.com)
  → explorer.rigorchain.com     (CORS: Allow-Origin https://www.rigorchain.com)
  → rpc.rigorchain.com          (CORS + POST JSON-RPC, read-only allowlist)
```

Static site on GitHub Pages does not run a backend; CORS must be configured on explorer and RPC proxy hosts.

---

## 9. Error handling model (§9)

| Condition | UI state | Banner |
|-----------|----------|--------|
| RPC unreachable | `offline` or `degraded` | Red: rigord RPC unreachable |
| Explorer unreachable | `degraded` | Amber: Explorer offline — RPC-only |
| `getblockconvergence` missing | Tab `degraded` | Amber per-field |
| `equivalence_claimed === true` | `degraded` + fault | Red: constitutional fault |
| Partial JSON | `degraded` | Amber: incomplete node response |
| Stale (`> 60s`) | show data + amber stale chip | — |

**Never:** empty panel pretending success; mock height `0`; demo transaction hashes.

---

## 10. Security

- No secrets in frontend bundle
- RPC proxy must not expose wallet RPC
- Explorer is untrusted display — label on every enriched field
- CSP headers on Pages host: restrict script to self + KaTeX CDN if used

---

## 11. References

- [FRONTEND_SPECIFICATION.md](./FRONTEND_SPECIFICATION.md)
- [COMPONENT_DESIGN_SYSTEM.md](./COMPONENT_DESIGN_SYSTEM.md)
- [explorer-indexer-mvp](../../explorer)
