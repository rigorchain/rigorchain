# RIGOR Public Site — UI Layout Specification

**Task:** ``
**Document:** Component and module layout specification  
**Parent:** [SITE_ARCHITECTURE.md](./SITE_ARCHITECTURE.md)

| Field | Value |
|-------|-------|
| `equivalence_claimed` | **false** (displayed on all modules) |

---

## 1. Design intent

UI modules render **publication content** (static) and **live observables** (dynamic) in separate visual regions. Users must always see which region they are reading.

| Region type | Visual treatment |
|-------------|------------------|
| **Static publication** | White/neutral panel, "Specification" tag |
| **Live observable** | Bordered panel, `source: rpc` or `source: explorer`, timestamp |
| **Error / degraded** | Amber/red stripe; explicit message — no empty panels |

---

## 2. UI modules

### 2.1 Protocol Viewer (`ProtocolViewer`)

**Route:** `/protocol`  
**Content source:** [RIGOR_UNIFIED_WHITEPAPER_v1.0.md](../../docs/WHITEPAPER.md) §1

#### Structured tree view

Collapsible tree (left sidebar or top accordion on mobile):

```text
Protocol
├── Overview (UTXO · F(S,T) · no global state)
├── ORU
│   ├── Role (execution / fee / resource)
│   ├── Conservation equation
│   └── Subsidy schedule (1000→…→125)
├── RES
│   ├── Role (scarce settlement)
│   ├── Conservation equation
│   └── Mint / cap gates
├── FORGE
│   ├── Destroy-to-mint at destroy_cost(h) → RES path
│   ├── |F| ≥ 2 rule
│   └── PCORB validation
├── Consensus
│   ├── DurinHash PoW (reference)
│   └── 80-byte header invariant
└── UTXO
    ├── Script-as-compliance
    └── Transaction model
```

#### Section layout

| Zone | Width | Content |
|------|-------|---------|
| Tree nav | 3 cols | Anchor links; expand/collapse |
| Body | 9 cols | Markdown-rendered §1 sections |
| Live strip (optional) | Full width below | `getblockchaininfo` height + chain label |

**Collapsible technical sections:** Each major node (ORU, RES, FORGE, UTXO) defaults **expanded** on desktop; **collapsed** on mobile except active section.

**Live overlay on Protocol page:** Small `LiveProtocolStrip` showing current height and best block hash (RPC). Failure → amber strip "RPC unavailable — specification static only".

---

### 2.2 Axiom Viewer (`AxiomViewer`)

**Route:** `/axioms`  
**Content source:** Publication §2 + `AXIOMS_v1.0.md`

#### Mathematical block rendering

- Use **KaTeX** (or equivalent) for inline `\(...\)` and display `\[...\]` blocks
- Pre-render at build time where possible; hydrate on client for dynamic refs

#### Invariant panels (fixed grid 2×2)

| Panel ID | Title | Key equation |
|----------|-------|--------------|
| `INV-DET` | Determinism | \(S_{t+1} = F(S_t, T_t)\) |
| `INV-UTXO` | Conservation (ORU) | \(\sum \mathrm{ORU}_{in} = \sum \mathrm{ORU}_{out} + fee\) |
| `INV-RES` | Conservation (RES) | \(\sum \mathrm{RES}_{in} = \sum \mathrm{RES}_{out}\) |
| `INV-FAIR` | Fairness (`E = 246`) | \(C = 1024\) ORU/mint; Historical RFC-014: \(1/65536\) |
| `INV-CONV` | Convergence stability (125) | \(1000 \to 500 \to 250 \to 125,\; 125 = 5^3\) |
| `INV-EQUIV` | Non-equivalence | `equivalence_claimed = false` |

Each panel:

- Header + equation (math font)
- 2–3 sentence plain-language gloss
- Footer link to full axiom section

#### Page layout

```text
┌─────────────────────────────────────────┐
│ Invariant panel grid (6 panels)          │
├─────────────────────────────────────────┤
│ Full axiom document (scrollable MD)      │
└─────────────────────────────────────────┘
```

---

### 2.3 Lab Viewer (`LabViewer`)

**Route:** `/lab`  
**Content source:** Publication §3

#### Narrative sections

| Section | Component |
|---------|-----------|
| 3.1 System interpretation | `LabProse` |
| 3.2 Experimental layers | `LayerDiagram` (three columns) |
| 3.3 Separation principle | `SeparationBanner` (non-equivalence) |
| 3.4 Experimental philosophy | Bullet grid |
| 3.5 Positioning statement | Pull quote block |

#### Experiment tags (visible chips)

| Tag | Color token | Meaning |
|-----|-------------|---------|
| `PROOF-034` | `--tag-runtime` | Runtime archive verification |
| `RUNTIME-SOAK` | `--tag-runtime` | 2.1M regtest soak |
| `FORMAL-PROOF` | `--tag-formal` | Coq / PROOF-001–033 |
| `VISUALIZATION` | `--tag-observe` | Explorer / GUI — non-authoritative |

#### Layer separation diagram

```text
  Protocol          Formal Proof       Runtime Archive      Visualization
  ─────────         ────────────       ───────────────      ─────────────
  Engineering       Logical            Empirical τ          Read-only UI
  truth             derivations        + harness            (this website)
        ≠                   ≠                  ≠
```

**Rule:** Diagram is static; no animation. Chips on `/lab` are not clickable promises of live data.

---

### 2.4 Explorer Module (`ExplorerModule`)

**Route:** `/explorer`  
**Mode:** Read-only blockchain state viewer

#### Tab layout

| Tab | Primary binding | Content |
|-----|-----------------|---------|
| **Chain** | RPC `getblockchaininfo`, `getnetworkinfo` | Height, tip hash, IBD flag, peers |
| **Convergence** | RPC `getconvergencestatus`, `getblockconvergence` | Destroy cost and cumulative destroyed ORU |
| **RES mints** | Explorer `GET /api/res/mints` | Mint list. Tip cost from `getconvergencestatus` |
| **Block** | Explorer `GET /api/block/:hash` | Block detail + convergence panel |
| **Transaction** | Explorer `GET /api/tx/:id` | Tx + witness panels |
| **Lineage** | Explorer `GET /api/lineage/:txid` | Graph view (read-only) |

#### Fallback behavior (mandatory)

| Condition | UI |
|-----------|-----|
| Explorer down | Red banner: "Explorer API offline — RPC-only mode" or full offline |
| RPC down | Red banner: "rigord RPC unreachable — no live chain data" |
| Partial fields | Amber per-field `—` with tooltip "not reported by node" |
| **Mock data** | **FORBIDDEN** — show empty state + error, never synthetic rows |

#### Explorer chrome

```text
┌──────────────────────────────────────────────────────────┐
│ ExplorerModuleToolbar — refresh, last fetch, source badges  │
├──────────┬───────────────────────────────────────────────┤
│ Tab bar  │ Tab content (per-module loading/error/live)    │
└──────────┴───────────────────────────────────────────────┘
```

**30s auto-refresh** per [ROUTE_AND_DATA_BINDING_SPEC.md](./ROUTE_AND_DATA_BINDING_SPEC.md). Manual refresh resets timer.

---

### 2.5 System Status Bar (`SystemStatusBar`)

**Scope:** Global (all pages)

```text
[ GLOBAL: ONLINE ]  height: 381443  |  RPC: 42ms  |  Explorer: OK  |  equiv: false  [↻]
```

Compact on mobile: tap expands drawer with full §6 fields from SITE_ARCHITECTURE.

---

### 2.6 Verification Panel (`VerificationPanel`)

**Route:** `/verification`

| Widget | Data |
|--------|------|
| PROOF-034 progress | Static milestone table + optional ops JSON if published |
| Evidence grade | E0–E4 legend |
| Golden vectors | 12-family checklist (static; live count only if operator publishes manifest URL) |
| Release tiers | CT GO / PT CONDITIONAL / M NO-GO badges |

No fabricated progress percentages without a real data source.

---

### 2.7 Shared components

| Component | Purpose |
|-----------|---------|
| `EquivalenceBadge` | Always `false`; fault styling if API says `true` |
| `ReadOnlyBanner` | Global non-dismissible |
| `SourceLabel` | `rpc` \| `explorer` \| `static` + ISO timestamp |
| `DegradedField` | `—` + amber border |
| `ErrorPanel` | Message + retry + last error time |
| `LoadingSkeleton` | Neutral pulse — no fake numbers in skeleton |

---

## 3. Responsive breakpoints

| Breakpoint | Layout |
|------------|--------|
| `< 768px` | Single column; tree nav → dropdown |
| `768–1024px` | 4/8 column split |
| `> 1024px` | 3/9 split per SITE_ARCHITECTURE |

---

## 4. Accessibility

- All status colors paired with text labels (not color-only)
- Math blocks have `aria-label` with plain-text fallback
- Focus order: status bar → nav → banner → main → footer

---

## 5. References

- [SITE_ARCHITECTURE.md](./SITE_ARCHITECTURE.md)
- [ROUTE_AND_DATA_BINDING_SPEC.md](./ROUTE_AND_DATA_BINDING_SPEC.md)
- [STYLE_GUIDE.md](./STYLE_GUIDE.md)
