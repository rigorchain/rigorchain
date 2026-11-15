# Witness V1 Display Rules (Explorer)

**Document status:** Production documentation  
**Baseline:** `mainnet-freeze-rfc014-2026-05-25`  
**Sources:** [`../SCRIPT_STRUCTURE.md`](../SCRIPT_STRUCTURE.md). RFC-008 is historical and is not protocol authority.

---

## 1. Terminology lock

| Use in UI/docs | Do NOT use |
|----------------|------------|
| Witness **v1** | Taproot, BIP341, x-only pubkey path |
| **OP_CONVERGE** | Convergence destroy (not a witness v1 program); same tx MAY mint RES |
| **RES** program | “P2TR”, “Taproot address” |
| **ORU** witness v0 | — |

Historical RFC-014 / retired design: Witness v1 prefix ASCII `CVG2` was an earlier convergence encoding. It is **not** a live consensus script. Do not label live Convergence as “CVG2” or “Witness v1 convergence.” `OP_CONVERGE` is the only live Convergence classification.

Development databases created before script alignment may contain retired
classifications. Public network launch will rebuild the explorer view and
indexed observations from genesis using current consensus classification
rules. No production
CVG2 migration is required. Historical CVG2 conversion is not a consensus
concern.

“Script path” is retired vocabulary for RES: SECURITY-001-A / A-7 left a witness v1 RES output with
exactly one spending path, the Ed25519 key path. The PCORB proof is not a RES spend path at all — it
rides on the **ORU** funding input of a body mint transaction.

RIGOR witness v1 is **not** Bitcoin Taproot. Display MUST NOT imply BIP341 semantics.

---

## 2. Program classes

| Class | Prefix / rule | Asset domain | Display label |
|-------|---------------|--------------|---------------|
| **OP_CONVERGE** | 22-byte lock: `OP_PUSHBYTES_20 \|\| HASH160 \|\| OP_CONVERGE (0xbd)` | ORU | Script: `OP_CONVERGE`. Asset: ORU. State: Unspendable destroy; same tx MAY mint RES |
| **RES2** | Witness v1, wire prefix `RES2` | RES | “RES (RES2)” |
| **Native RES** | Witness v1, 32-byte Ed25519 pubkey | RES | “RES (native)” |
| **Retired CVG2 v1** | Historical RFC-014 / retired design: first 4 bytes `CVG2` | NONE (not a third asset) | “Historical RFC-014 / retired Witness v1 CVG2 (not OP_CONVERGE)” |
| **Unknown v1** | Not matching above | Invalid at consensus | “Invalid v1 program” |

Addresses: testnet Bech32 HRP **`trg`**; v1 programs use **`trg1p`** style per descriptor policy — not `bc1p`.

---

## 3. Retired PCORB — do not display

Historical PCORB (tagged coinbase carrier / 6-item mint witness) is not live
issuance. Consensus rejects every carrier (`bad-pcorb-carrier`). Explorer and
product UI MUST NOT render a PCORB table, field names, or `convergence_root`.

Live mint is `CheckResMintArithmetic` on a body transaction: `OP_CONVERGE`
destroys ORU and the same transaction MAY create Witness v1 RES.

**Sidebar disclaimer (required on mint pages):**

> Mint cost is `destroy_cost(h)` at the confirming height (`R · (2²⁵+h)² ≤ B << 40`).
> Parent `nBits` is not a mint price. Lottery `res_probability_target` is retired.

---

## 4. txid / wtxid display rules

| Context | Show txid | Show wtxid |
|---------|-----------|------------|
| Transaction page header | **Primary** | Secondary, labeled “Witness txid” |
| Convergence candidate tables | **Only txid** | Hide or footnote |
| Convergence lineage graph | **Only txid** | Forbidden as edge key |
| RES mint linkage | **txid** | Never substitute wtxid |
| Mempool (optional) | txid primary | wtxid for package debug |

**Forbidden:** URL routes like `/tx/{wtxid}` as canonical.

---

## 5. Spending a native RES output — one path only

| Path | Stack depth | Display |
|------|-------------|---------|
| Key-path transfer | 2 items | “RES key-path spend” |
| Anything else | any | Red: “invalid RES spend” — consensus rejects it |

**SECURITY-001-A / A-7.** A witness v1 RES output has exactly one spending path: the 2-item Ed25519
key path, whose signature commits to the spending transaction through the sighash. The retired
11-item script path is not displayed. The explorer MUST NOT offer, decode, or label any RES spend
as a script path.

Do not show a Bitcoin-style control block diagram. Optional: collapsible hex per item.

---

## 6. Reserved witness slots — removed (A-7)

Retired mint-proof reserved slots are not displayed. Trailing witness pushes on an ORU input are
ordinary witness data, not a proof table.

**Future extensions:** Require new RFC — UI MUST NOT guess semantics.

---

## 7. Canonicality notes

| Topic | Rule |
|-------|------|
| Witness serialization | Multiple witness serializations may exist pre F-WX-08; post enforcement, placeholders MUST be zero |
| Chainstate identity | Explorer lists **txid** for set membership |
| Program length | Consensus v1 program MUST be 32 bytes |

---

## 8. Coinbase witness (display)

Do not label coinbase as RES mint unless `rigor_res_minted` on block refers to **body** mint.

---

## 9. Document metadata

| Field | Value |
|-------|--------|
| Risk classification | **MEDIUM** (terminology); **HIGH** if Taproot implied |
| Explorer classification | **DISPLAY-RULES** |
| Consensus impact | **NONE** |
| Replay impact | **INDIRECT** — mislabeled bind fields mislead auditors |
| Determinism impact | **NONE** |
| Indexing risk | **MEDIUM** |
| Minimal safe recommendation | RES key path only; txid primary; no PCORB table; forbidden Taproot vocabulary |
