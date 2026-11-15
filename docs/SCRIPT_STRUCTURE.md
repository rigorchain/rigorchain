# RIGOR Script Structure

**Version:** 3.0  
**Date:** 2026-09-12  
**Purpose:** Live production script surface  
**Implementation:** `core/src/script/rigor_classify.cpp`, `core/src/script/predicate_verify.cpp`,
`core/src/script/rigor_bytes.h`

```
equivalence_claimed = false
documentation_consensus_authority = false
```

Production RIGOR has **no Script VM**. There is no `EvalScript`, no
`ExecuteOpcode`, and no `interpreter.cpp`. `CScript` is a byte container.
Spends go through `VerifyRigorPredicate`.

Protocol specifications are normative SSOT.
Implementations realize the frozen protocol.

---

## 1. Architecture

```
scriptPubKey bytes
        ↓
ClassifyRigorTxout          (shape)
        ↓
AssetTypeFromScriptPubKey   (ORU / RES / NONE / INVALID)
        ↓
VerifyRigorPredicate        (spend, only if the output is a UTXO)
```

Classification is not issuance authority. Script alone cannot mint ORU or RES.

Live Convergence is `RIGOR_MARKER_CONVERGE` (`0xbd`), not a witness program.

---

## 2. Data structures

### 2.1 CScript

**Location:** `core/src/script/script.h`

```cpp
typedef prevector<28, unsigned char> CScriptBase;

class CScript : public CScriptBase {
    SERIALIZE_METHODS(CScript, obj) { READWRITEAS(CScriptBase, obj); }
    bool GetOp(const_iterator& pc, opcodetype& opcodeRet,
               std::vector<unsigned char>& vchRet) const;
    bool IsWitnessProgram(int& version, std::vector<unsigned char>& program) const;
};
```

`CScript` stores bytes. `GetOp` parses pushes. It does not execute opcodes.

### 2.2 CScriptWitness

**Location:** `core/src/script/script.h`

```cpp
struct CScriptWitness {
    std::vector<std::vector<unsigned char>> stack;
};
```

Live spends use a two-item witness: `<sig64> <pk32>`.

### 2.3 CTxOut

**Location:** `core/src/primitives/transaction.h`

```
CTxOut = (nValue, scriptPubKey)
```

There is no `(asset, value, script)` field. Asset class is
`AssetTypeFromScriptPubKey(scriptPubKey)`.

---

## 3. Live templates

Source: `ClassifyRigorTxout` / `AssetTypeFromScriptPubKey` in
`core/src/script/rigor_classify.cpp`.

| Template | Encoding | `TxoutType` | Asset | Spendable |
|----------|----------|-------------|-------|-----------|
| ORU P2WPKH | `00 14` + 20-byte HASH160 | `WITNESS_V0_KEYHASH` | ORU | Yes |
| RES v1 | `51 20` + 32-byte Ed25519 program | `WITNESS_V1_RES` | RES | Yes |
| Converge | `14` + 20-byte HASH160 + `bd` (22 bytes) | `CONVERGE` | ORU | **No** (not a UTXO) |
| NULL_DATA | `6a` + pushes, `nValue = 0` | `NULL_DATA` | NONE | No |

Anything else is `NONSTANDARD`.

`ClassifyRigorTxout` also recognizes a `0xbb` template as `TxoutType::RES_MINT` and `AssetTypeFromScriptPubKey` returns `ASSET_RES` for that shape. Recognition is not acceptance. `CheckTransaction` rejects a non-coinbase output with that script (`bad-txns-res-mint-noncoinbase`). On mainnet a coinbase RES amount other than 0 is `bad-cb-res-amount`. `0xbb` is not a valid RES mint marker.

Not creatable: P2PKH, P2SH, P2WSH, P2PK, bare multisig, Taproot, secp256k1
CHECKSIG.

### 3.1 ORU Witness v0

```
scriptPubKey: 00 14 <20-byte HASH160(Ed25519 pk)>
witness:      <64-byte sig> <32-byte pk>
```

Sighash domain tag `RGRORU01`.

### 3.2 RES Witness v1

```
scriptPubKey: 51 20 <32-byte Ed25519 pubkey>
witness:      <64-byte sig> <32-byte pk>   // pk must equal the program
```

Not Bitcoin Taproot. No control block, annex, or tapscript.
Sighash domain tag `RGRRES01`.

Classification is a pure byte predicate. On-curve / canonical Ed25519 is
**not** a ConnectBlock rule.

RES is minted as ordinary Witness v1 outputs on a body transaction that
also destroys ORU. Consensus checks `CheckResMintArithmetic(R, B, h)`:
`R > 0 → R · (2²⁵+h)² ≤ B << 40` (`R = 0` unconstrained).
`RES_COIN = 100000000`. Cap `419430400000000` atoms (`2²²` RES).
Mint-output maturity 100 is reorg protection.

### 3.3 Convergence destroy

```
scriptPubKey: 14 <20-byte HASH160(pk32)> bd
nValue     ≥ 1 atom
```

Rules (`IsConvergeScript` / `ParseConvergeScript`):

1. Exactly 22 bytes.
2. Exactly one positive-value converge output per transaction.
3. Inputs must be ORU.
4. Output is classified ORU but **does not enter the UTXO set**.
5. Destroy amount `B` is the mint-inequality input.

### 3.4 NULL_DATA

`RIGOR_MARKER_RETURN` (`0x6a`) plus push-only data. `nValue` must be 0.
Not an asset.

---

## 4. Markers and parser bytes

| Name | Byte | Role |
|------|------|------|
| `RIGOR_MARKER_RETURN` | `0x6a` | NULL_DATA |
| `RIGOR_MARKER_CONVERGE` | `0xbd` | ORU destroy; same tx MAY mint Witness v1 RES |
| `RIGOR_PUSH_0` | `0x00` | Witness v0 version / push empty |
| Direct push | `0x01`–`0x4b` | Length-prefixed push |
| `RIGOR_PUSHDATA1/2/4` | `0x4c`–`0x4e` | Long pushes |
| `RIGOR_PUSH_1`…`RIGOR_PUSH_16` | `0x51`–`0x60` | Small integers / witness v1 version |

---

## 5. Coinbase scriptSig

**Location:** `core/src/consensus/rigor_coinbase_layout.h`

```
<BIP34 height>
```

Height push only. The coinbase does not mint RES.

---

## 6. Signature domain

Ed25519 only. Fixed 64-byte signature. No hashtype byte.

```
SHA256d(tag || chain_id || nVersion || hashPrevouts || hashSequence
        || hashOutputs || this prevout || spent scriptPubKey
        || amount || nSequence || nLockTime)
```

Tags: `RGRORU01` / `RGRRES01`. `chain_id` is genesis hash, passed as an
explicit `SignatureHash` argument — never a consensus global.

---

## 7. Serialization (unchanged wire)

### Transaction

```
[4] nVersion
[var] vin
[var] vout
[4] nLockTime
```

Witness form adds marker `0x00` and flag `0x01` after version, then per-input
witness stacks before locktime.

### CTxOut

```
[8] nValue (LE)
[var] scriptPubKey
```

### UTXO Coin (`core/src/coins.h`)

```
[VARINT] code = (nHeight << 1) | fCoinBase
[var] TxOutCompression(out)
[VARINT] nFlags (optional, non-zero only)
```

---

## 8. Examples

### ORU P2WPKH

```
00 14 <20-byte HASH160>
```

### RES

```
51 20 <32-byte Ed25519 pubkey>
```

### Convergence destroy

```
14 <20-byte HASH160> bd
```

`nValue` is the amount. It is **not** encoded in the script.

---

**End of document**
