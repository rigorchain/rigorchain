# RIGOR Official Site — Component Design System

**Task:** ``
**Parent:** [ARCHITECTURE_OVERVIEW.md](./ARCHITECTURE_OVERVIEW.md)  
**Extends:** [../public-site/STYLE_GUIDE.md](../public-site/STYLE_GUIDE.md)

| Field | Value |
|-------|-------|
| `equivalence_claimed` | **false** |

---

## §5 — Component design system

Deterministic UI for `www.rigorchain.com`. Scientific terminal aesthetic; no marketing or hype animation.

---

## §7 — Visual design philosophy

| Attribute | Spec |
|-----------|------|
| **Style** | Scientific terminal — grid, monospace data, calm surfaces |
| **Layout** | Deterministic 12-column grid, `max-w-6xl` content |
| **Language** | Publication corpus only — no hype copy |
| **Motion** | Opacity transitions ≤150ms; no bounce, particles, or slot effects |

### Colors

| Token | Hex | Meaning |
|-------|-----|---------|
| `--status-verified` | `#2d6a4f` | Green — live OK, deterministic match |
| `--status-degraded` | `#b08900` | Amber — partial / stale |
| `--status-failure` | `#9b2226` | Red — hard error, constitutional fault |
| `--status-offline` | `#6c757d` | Gray — static-only / RPC down |

Tailwind mapping: `text-status-verified`, `bg-status-degraded/10`, etc.

### Typography

| Role | Stack | Usage |
|------|-------|-------|
| **Mono** | `IBM Plex Mono`, monospace | Hashes, heights, RPC JSON |
| **Sans** | `Inter`, system-ui | Prose, nav, labels |
| **Math** | KaTeX fonts | `/axioms` equations |

---

## Global UI rules

| Rule | Component |
|------|-----------|
| Read-only indicator always visible | `ReadOnlyBanner` |
| `equivalence_claimed: false` always displayed | `EquivalenceBadge` |
| No hidden state transitions | `SourceLabel` on all live values |
| No mock placeholders in loading | `LoadingSkeleton` (no digits) |
| Failures explicit | `ErrorBanner`, `DegradedBanner` |

---

## Core components

### `EquivalenceBadge`

```tsx
// Props: { liveValue?: boolean | null; fetchedAt?: string }
// Default display: equivalence_claimed: false
// Fault: liveValue === true → red fault variant
```

### `ReadOnlyBanner`

Fixed below nav. Text: *Read-only observer. rigord is consensus authority. No signing or submission.*

### `SystemStatusBar`

Aggregate badge: `ONLINE` | `DEGRADED` | `OFFLINE` | `SYNCING` + height + RPC/explorer latency.

### `SourceLabel`

```text
{value}  [rpc · 2026-06-13T12:00:00Z]
```

Props: `source: 'rpc' | 'explorer' | 'static' | 'manifest'`

---

### `ProtocolCard`

**Purpose:** ORU / RES / FORGE / overview state display (static + optional live strip).

| Variant | Content |
|---------|---------|
| `overview` | UTXO, \(F(S,T)\), no global state |
| `oru` | Conservation, flat subsidy 128 ORU (`h ≥ 1`) |
| `res` | Conservation, cap, mint path |
| `forge` | Destroy-to-mint at `destroy_cost(h)`, cap `2²²` RES |
| `utxo` | Script-as-compliance |

**Layout:**

```text
┌─────────────────────────────────────┐
│ [tag: SPECIFICATION]     ProtocolCard │
├─────────────────────────────────────┤
│ Title (sans)                          │
│ Body (prose)                          │
│ Equation (math) optional              │
├─────────────────────────────────────┤
│ Live strip (mono) — optional          │
└─────────────────────────────────────┘
```

Props: `variant`, `live?: { height?, hash? }`, `status?: 'live'|'static'|'degraded'`

---

### `ConvergencePanel`

**Purpose:** RFC-020 same-tx destroy-to-mint explainer (read-only).

| Field | Binding | Display |
|-------|---------|---------|
| Where / Rule / Height | Static protocol copy | Body tx destroys ORU and may create RES |
| Live issuance | `/api/status` via `Rfc020LivePanel` | Confirming height and destroy cost |

**States:** `loading` | `live` | `degraded` | `error`

```text
┌─ ConvergencePanel ─────────────────────┐
│ How a mint is admitted                 │
│ Same body tx: OP_CONVERGE + optional   │
│ Witness v1 RES                         │
└────────────────────────────────────────┘
```

---

### `MintPanel`

**Purpose:** RES lifecycle visualization (observer mode).

| Section | Data |
|---------|------|
| Mint list table | `GET /api/res/mints` |
| Status sidebar | `getconvergencestatus` |
| Lifecycle diagram | Static: eligibility → mint tx → RES UTXO |

**Rules:** Empty list = empty state message, not demo rows.

Columns: `height`, `txid`, `amount`, `block_hash` (as available from API).

---

### `NetworkPanel`

**Purpose:** Peer / latency / chain status for `/network`.

| Widget | Source |
|--------|--------|
| `connections` | `getnetworkinfo.connections` |
| `peers` | `getpeerinfo` if exposed by proxy (optional) |
| `ibd` | `getblockchaininfo.initialblockdownload` |
| `verificationprogress` | `getblockchaininfo.verificationprogress` |
| `difficulty` | `getblockchaininfo.difficulty` |
| RPC latency | Adapter timing |
| Explorer `/healthz` | HTTP status |

Read-only mining health: chain work / block rate from RPC — **no** worker or pool controls.

---

### `VerificationPanel`

**Purpose:** PROOF-034 / GV / P03 / soak state.

| Block | Content |
|-------|---------|
| PROOF-034 milestones | M0–M7 table |
| Height progress | From manifest or static "in progress" |
| GV grid | 12 families PASS/PARTIAL/NE |
| P03 label | `P03_PARTIAL` (baseline) |
| Evidence grade | E0–E4 chips |
| Release tiers | CT / PT / M badges |

No fabricated percentages without manifest URL.

---

## Supplementary components

| Component | Use |
|-----------|-----|
| `ErrorBanner` | Red full-width |
| `DegradedBanner` | Amber full-width |
| `MismatchBanner` | RPC vs explorer conflict |
| `LoadingSkeleton` | Pulse blocks without numbers |
| `EmptyState` | Icon + message + retry |
| `ExperimentTag` | PROOF-034, RUNTIME-SOAK, etc. |
| `InvariantPanel` | Axiom grid cell |
| `LayerDiagram` | Lab separation SVG/ASCII |
| `Footer` | Disclaimer + build version |

---

## Component state pattern

```typescript
type PanelState = 'idle' | 'loading' | 'live' | 'degraded' | 'error';

type PanelProps<T> = {
  state: PanelState;
  data: T | null;
  error: string | null;
  fetchedAt: string | null;
  source: 'rpc' | 'explorer' | 'static' | 'manifest';
  onRefresh?: () => void;
};
```

Every core panel implements this interface for consistent UX.

---

## Tailwind token extension (spec)

```javascript
// tailwind.config — excerpt
theme: {
  extend: {
    colors: {
      status: {
        verified: '#2d6a4f',
        degraded: '#b08900',
        failure: '#9b2226',
        offline: '#6c757d',
      },
    },
    fontFamily: {
      mono: ['"IBM Plex Mono"', 'ui-monospace', 'monospace'],
      sans: ['Inter', 'system-ui', 'sans-serif'],
    },
  },
},
```

---

## Accessibility

- Status colors always paired with text (`ONLINE`, not green dot alone)
- `aria-live="polite"` on `SystemStatusBar` when status changes
- KaTeX: `aria-label` with plain-text equation repeat

---

## §10 — Acceptance

| Field | Status |
|-------|--------|
| Component system | **COMPLETE** |
| Design system | **COMPLETE** |
| `equivalence_claimed` badge | **COMPLETE** |
| No mock / hype UI | **COMPLETE** |
| Freeze compliance | **COMPLETE** |

---

## References

- [FRONTEND_SPECIFICATION.md](./FRONTEND_SPECIFICATION.md)
- [API_BINDING_LAYER.md](./API_BINDING_LAYER.md)
- [DEPLOYMENT_GITHUB_PAGES_AND_DOMAIN.md](./DEPLOYMENT_GITHUB_PAGES_AND_DOMAIN.md)
- [RIGOR_UNIFIED_WHITEPAPER_v1.0.md](../../docs/WHITEPAPER.md)
