# RIGOR Official Site — Architecture Overview

**Task:** ``
**Title:** Production website system — official domain + GitHub deployment  
**Date:** 2026-06-13  
**Type:** Frontend + static site + API binding specification only  
**Domain:** [https://www.rigorchain.com](https://www.rigorchain.com)  
**Repository:** [https://github.com/rigorchain/rigorchain](https://github.com/rigorchain/rigorchain)  
**Parent specs:** [web/public-site/](../public-site) 
**Content authority:** [RIGOR_PROTOCOL_IDENTITY.md](../../docs/whitepaper/RIGOR_PROTOCOL_IDENTITY.md)  
Older site specs may use earlier release-channel names.

| Field | Value |
|-------|-------|
| Architecture Impact | **NONE** (new static site package spec) |
| Consensus Impact | **NONE** |
| Protocol Impact | **NONE** |
| Mining Impact | **NONE** |
| `equivalence_claimed` | **false** |

---

## §1 — System goal

Design a **production-grade official website** for RIGOR at `www.rigorchain.com` that presents:

| Dimension | Public framing |
|-----------|----------------|
| **Deterministic blockchain** | UTXO state machine; \(S_{t+1} = F(S_t, T_t)\) |
| **Dual-asset protocol** | Native assets **ORU** (Witness v0) and **RES** (Witness v1) — per frozen naming (`PROTOCOL_NAMING_TRUTH_SOURCE.md`) |
| **Formal verification-driven** | PROOF catalog + PROOF-034 runtime archive (independent layers) |
| **Scientific + engineering hybrid** | Protocol truth + axiom math + lab narrative + live observer |

### Exposed surfaces (read-only)

| Surface | Route | Data |
|---------|-------|------|
| Protocol state | `/explorer`, `/network` | RPC + explorer |
| Convergence visualization | `/explorer` | `getconvergencestatus`, `getblockconvergence` |
| RES mint lifecycle (observer) | `/explorer` | `/api/res/mints`, `getconvergencestatus` |
| Mining + network health | `/network` | `getnetworkinfo`, `getblockchaininfo` |
| Formal verification status | `/verification` | PROOF-034, P03, golden vectors (GV) |

**Non-goals:** Wallet UI, mining controls, transaction sending, `rigord` backend changes.

---

## §2 — High-level architecture

```text
                    ┌─────────────────────────────────────┐
                    │  CDN / GitHub Pages                  │
                    │  https://www.rigorchain.com          │
                    │  Static SPA (React + Vite export)    │
                    └──────────────┬──────────────────────┘
                                   │ HTTPS (browser)
           ┌───────────────────────┼───────────────────────┐
           ▼                       ▼                       ▼
   ┌───────────────┐      ┌───────────────┐      ┌─────────────────┐
   │ Read-only RPC │      │ Explorer API  │      │ Static manifest │
   │ proxy → rigord│      │ (primary pub) │      │ PROOF/GV JSON   │
   │ (authority)   │      │ enrichment    │      │ (optional CDN)  │
   └───────────────┘      └───────────────┘      └─────────────────┘
```

### Layer bridge

| Layer | Site module | Authority |
|-------|-------------|-----------|
| **Protocol (rigord)** | RPC adapters | Validating node |
| **Explorer** | HTTP adapters | Indexer mirror |
| **Formal verification** | `/verification` static + manifest | Harness artifacts |
| **Lab narrative** | `/lab`, `/axioms` | Publication corpus |
| **Public visualization** | All live widgets | Derived read-only state |

---

## §3 — Package layout (repository)

Proposed path in `rigorchain` monorepo:

```text
web/rigorchain-site/
├── ARCHITECTURE_OVERVIEW.md          (this file)
├── FRONTEND_SPECIFICATION.md
├── API_BINDING_LAYER.md
├── DEPLOYMENT_GITHUB_PAGES_AND_DOMAIN.md
├── COMPONENT_DESIGN_SYSTEM.md
├── package.json                      (implementation phase)
├── vite.config.ts
├── src/
│   ├── app/                          routes, shell, state
│   ├── components/                   design system
│   ├── adapters/                     rpc, explorer
│   ├── content/                      synced publication MD
│   └── styles/
├── public/
│   └── CNAME                         www.rigorchain.com
└── .github/workflows/deploy-site.yml
```

Build output: `web/rigorchain-site/dist/` → deployed to GitHub Pages or Pages-compatible artifact.

---

## §4 — Route map (production)

| Route | Purpose |
|-------|---------|
| `/` | Landing — protocol summary, live status, layer map |
| `/protocol` | ORU / RES / FORGE / UTXO / DurinHash PoW |
| `/axioms` | Mathematical foundation (`E = 246` / `C = 1024`, 125, invariants) |
| `/lab` | Scientific narrative + layer separation |
| `/explorer` | Live blockchain state viewer |
| `/network` | Node + peer + chain health visualization |
| `/verification` | PROOF-034, P03, GV system status |

Extends [public-site routes](../public-site/SITE_ARCHITECTURE.md) with `/network`; `/system-status` merged into `/network` + global header for production.

---

## §5 — Global shell (all pages)

| Element | Requirement |
|---------|-------------|
| Top navigation | All routes + GitHub link |
| `SystemStatusBar` | ONLINE / DEGRADED / OFFLINE / SYNCING |
| `equivalence_claimed: false` badge | Always visible |
| Read-only banner | Non-dismissible |
| Footer | PROOF-034 disclaimer, release tiers, build version tag |

---

## §6 — State management (summary)

Global `SystemState` is **derived read-only** — see [FRONTEND_SPECIFICATION.md](./FRONTEND_SPECIFICATION.md) §6.

No writes, no wallet, no mining RPC.

---

## §7 — Related documents

| Document | Scope |
|----------|-------|
| [FRONTEND_SPECIFICATION.md](./FRONTEND_SPECIFICATION.md) | Stack, pages, state |
| [API_BINDING_LAYER.md](./API_BINDING_LAYER.md) | RPC + explorer rules |
| [DEPLOYMENT_GITHUB_PAGES_AND_DOMAIN.md](./DEPLOYMENT_GITHUB_PAGES_AND_DOMAIN.md) | CI/CD, domain, CDN |
| [COMPONENT_DESIGN_SYSTEM.md](./COMPONENT_DESIGN_SYSTEM.md) | UI components + visuals |
| [../public-site/](../public-site) | baseline |

---

## §8 — Acceptance criteria

| Field | Status |
|-------|--------|
| Frontend architecture | **COMPLETE** (FRONTEND_SPECIFICATION) |
| API binding model | **COMPLETE** (API_BINDING_LAYER) |
| Domain deployment model | **COMPLETE** (DEPLOYMENT_GITHUB_PAGES_AND_DOMAIN) |
| Component system | **COMPLETE** (COMPONENT_DESIGN_SYSTEM) |
| State model | **COMPLETE** |
| CI/CD pipeline | **COMPLETE** |
| Design system | **COMPLETE** |
| Freeze compliance | **COMPLETE** |
| `equivalence_claimed` | **false** |

---

## §9 — Final result

This package defines **the official public website system for RIGOR (production grade)** — bridging protocol (`rigord`), explorer, formal verification, lab narrative, and deterministic public visualization at **www.rigorchain.com**.

Implementation code is a **separate engineering task**; delivers specification only.
