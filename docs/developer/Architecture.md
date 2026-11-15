# RIGOR Architecture

**Issuance:** RFC-020  
**Implementation:** `core/src/` (realization)

```
equivalence_claimed = false
documentation_consensus_authority = false
```

Protocol specification is normative SSOT.
Implementation follows protocol SSOT.
If this file disagrees with RFC-020, the RFC is correct.

---

## Core principle

**Script is the compliance / classification layer.** There is no Script VM.

`CScript` is a byte container. Asset *class* is
`AssetTypeFromScriptPubKey(scriptPubKey)`. Spends go through
`VerifyRigorPredicate`. Script alone cannot mint ORU or RES.

```
TxOut = (nValue, scriptPubKey)
Asset class = ClassifyRigorTxout → AssetTypeFromScriptPubKey
Spend = VerifyRigorPredicate
```

RES creation is a body transaction: one `OP_CONVERGE` destroy `B` and Witness v1
RES outputs summing to `R`, admitted by `CheckResMintArithmetic`.

---

## Design constraints

### Storage

- `CTxOut` is `(nValue, scriptPubKey)` only. No parallel asset field.
- Block header remains 80 bytes. Block **id** is SHA256d of those bytes.
- Live scripts are not Bitcoin Script programs. P2PKH / P2SH / P2WSH /
  Taproot are not creatable.

### Classification

Unrecognized scripts are `ASSET_INVALID` (`bad-rigor-script-type`). Never
defaulted to ORU.

```cpp
AssetType AssetTypeFromScriptPubKey(const CScript& scriptPubKey) {
    switch (ClassifyRigorTxout(scriptPubKey, sols)) {
    case TxoutType::WITNESS_V0_KEYHASH:
    case TxoutType::CONVERGE:
        return ASSET_ORU;
    case TxoutType::WITNESS_V1_RES:
        return ASSET_RES;
    case TxoutType::NULL_DATA:
        return ASSET_NONE;
    default:
        return ASSET_INVALID;
    }
}
```

Source: `core/src/script/rigor_classify.cpp`.

### Live templates

| Template | Encoding | Asset | Spendable |
|----------|----------|-------|-----------|
| ORU P2WPKH | Witness v0, 20-byte | ORU | Yes |
| RES v1 | Witness v1, 32-byte Ed25519 | RES | Yes |
| RES mint output | Witness v1, 32-byte | RES | After maturity 100 |
| Converge | `<HASH160> 0xbd` (22 bytes) | ORU | No (not a UTXO) |
| NULL_DATA | `0x6a` + pushes, `nValue = 0` | NONE | No |

Addresses / descriptors: `wpkh()` (v0/20 Bech32 `rg1q…`) and `res()`
(v1/32 Bech32m). Witness v1 is not Bitcoin Taproot.

### UTXO

- No account model as consensus truth.
- `AddCoins` skips outputs that do not `EntersUtxoSet` (Convergence destroy).
- No administrative registry. No oracle consensus.

### Dual-asset roles

| Asset | Role |
|-------|------|
| **ORU** | PoW subsidy, fees, Convergence destroy. `R(0) = 0`; then 128 ORU forever. |
| **RES** | Capped reserve (`2²²` RES). Body-tx destroy-to-mint. Not mined. |

PoW does not create RES. Eligible RES-transfer ORU fees destroy
`floor(fee/2)` (RFC-018). That share is not a UTXO.

**OpuHash** = SHA3-256(`"RGRPOW01" || header_80`). ASERT half-life 43200 s
(mainnet).

---

## Layer model

| Layer | Scope | Code |
|-------|-------|------|
| L1 Consensus | Header, OpuHash, txs, body-tx mint | `validation.cpp`, `consensus/`, `script/` |
| L2 Assets | ORU / RES under L1 | `rigor_classify.cpp`, `res_issuance.h`, `tx_verify.cpp` |
| L3 Wallet | Keys, signing | `wallet/` |
| L4 Interface | RPC, REST, P2P | `rpc/`, `rest/`, `net/` |

---

## Compatibility

- Header layout is 80 bytes. Non-RIGOR nodes reject RIGOR blocks.
- Convergence RPCs: `getconvergencestatus`, `getblockconvergence`.
- GBT copy-bytes: `default_coinbase_tx`, `default_coinbase_scriptsig`.

---

## Determinism

```
same input → same bytes
```

Mint arithmetic is integer only. `destroy_cost(h)` is not `nBits`.

---

## Monetary split

| Function | Path |
|----------|------|
| PoW security + fees | ORU coinbase subsidy (128 for `h ≥ 1`) |
| Convergence destruction | `<HASH160> 0xbd` (not a UTXO) |
| Fee destroy | RFC-018 share of RES-transfer ORU fees |
| Reserve issuance | Body tx: `OP_CONVERGE` + Witness v1 RES |

Network fees are ORU-only.

---

## References

- [`docs/developer/CODE_SSOT_PROTOCOL_FACTS.md`](./CODE_SSOT_PROTOCOL_FACTS.md)
- [`docs/specification/SPEC-000-CONSENSUS-SPECIFICATION.md`](../specification/SPEC-000-CONSENSUS-SPECIFICATION.md)
- [`docs/specification/RIGOR_OPCODE_SPECIFICATION.md`](../specification/RIGOR_OPCODE_SPECIFICATION.md)
- [`docs/SCRIPT_STRUCTURE.md`](../SCRIPT_STRUCTURE.md)
- [`docs/protocol/RFC-020.md`](../protocol/RFC-020.md)
- [`core/doc/mining-pool-gbt.md`](../../core/doc/mining-pool-gbt.md)
