# Implementation facts extracted from production C++ (P0-097)

> **Shipping issuance (RFC-020).** Continuous `destroy_cost(h)`, pole predicate
> `R · (2²⁵+h)² ≤ B << 40`, cap `2²²` RES, flat 128 ORU. Numeric SSOT:
> `tools/res_continuous_price_schedule.py`. Formal status:
> `audit/formal_bridge/FORMAL_STATUS_SOURCE_OF_TRUTH_v1.md`
> (`REFINEMENT_PROVED=false`).


**Document status:** Verification reference  
**Historical verification reference:** P0-097  
**Date:** 2026-09-03; PoW name aligned 2026-09-04 (OpuHash = SHA3-256 / `RGRPOW01`)  
**Nature:** Documentation extraction from production source. **No consensus modification.**

```
RFC-020 / RFC-018
 >
C++ realization
 >
tests / golden vectors
 >
this extraction (explanation)
```

This file records **current production facts** extracted from `core/src/`.
Issuance is specified by RFC-020. The C++ files named below realize it.
Implementation follows protocol SSOT. If this extraction disagrees with
RFC-020 / RFC-018, the RFC is correct.

Lock files:

- `core/src/consensus/params.h`
- `core/src/consensus/rigor_consensus_lock.h`

```
equivalence_claimed = false
documentation_consensus_authority = false
```

---

## 1. Network identity

Product display version is `RIGOR_RELEASE_VERSION` in
`core/src/clientversion.h`. Do not copy that string into other
markdown files.

| Field | Source |
|-------|--------|
| `RIGOR_RELEASE_VERSION` | `core/src/clientversion.h` (authoritative) |
| `RIGOR_RELEASE_CODENAME` | same header |
| Packed `CLIENT_VERSION` | `25.0.0` (`CLIENT_VERSION_MAJOR=25`) |
| `CLIENT_NAME` | `RigorCore` |
| Chain name lock | `RIGOR` (`rigor_consensus_lock.h`) |

The public product name is RIGOR. Display version is `RIGOR_RELEASE_VERSION`.

P2P handshake protocol id (`core/src/version.h`):

| Field | Value |
|-------|-------|
| Serialization `PROTOCOL_VERSION` | `70016` |
| `RIGOR_PROTOCOL_CURRENT_VERSION` | `1` |

### 1.1 Mainnet

Source: `core/src/kernel/chainparams.cpp` (`CMainParams`),
`core/src/chainparamsbase.cpp`.

| Field | Value |
|-------|-------|
| Chain id | `main` |
| Genesis hash | `e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468` |
| Genesis merkle root | `9d3fbd207af927fc8e2c57e975d8d5845913bf059d956592e8a0bcf38b3515dd` |
| Magic `pchMessageStart` | `ce f1 db fa` |
| P2P port | `8760` |
| RPC port | `8761` |
| Bech32 HRP | `rg` (`BECH32_HRP` lock) |
| Genesis nTime | `1792908000` (2026-10-25 06:00:00 UTC) |
| Genesis nNonce | `0` |
| Genesis nBits | `0x1d008000` |
| Genesis nVersion | `1` |
| Genesis timestamp | `Selection not computation. Convergence defines state. Naming is consensus.` |
| Genesis ORU subsidy | `0` |
| Coinbase maturity | `100` |
| Target spacing | `600` seconds |
| ASERT half-life | `43200` seconds (12 hours) |
| ASERT anchor height | `1` |
| `powLimit` | `0000007fffffffffffffffffffffffffffffffffffffffffffffffffffffffff` |

### 1.2 Other shipped networks

Signet is a leftover string constant only. There is no `CSignetParams`.

| Network | Genesis hash | Magic | P2P | RPC | HRP |
|---------|--------------|-------|-----|-----|-----|
| testnet (`test`) | `0aca439758fba7fa5190b7e1cf37f9447516ef4cade07a49e7d484ea5b67b9ce` | `0c 12 0a 08` | 18760 | 18761 | `trg` |
| regtest | `3070f47e2c1b153a3288382f3258396bb48b016eab76eae9f235d006dcc2af63` | `fa bf b5 da` | 28760 | 28761 | `rgrt` |
| devtest | `9762f59ecfe1050ca45e404dc95e32ae5a93b9a046e116f2f2f562e44f21dce0` | `1d 2a 3b 4c` | 48760 | 48761 | `dvt` |
| slowtest | `18ddf722cf24b35ad9e3b548ddba22a2e54535fd5932d4d699fa9602f6d3efb8` | `2e 3c 4d 5e` | 49760 | 49761 | `slt` |

Base58 mainnet prefixes remain Bitcoin-shaped (`0x00` / `0x05` / `0x80`) for
key-IO compatibility of the inherited encoder. Live receive addresses are
Bech32 `rg1q…` (ORU witness v0) and Bech32m (RES witness v1).

---

## 2. Asset model

Source: `core/src/primitives/transaction.h`,
`core/src/script/rigor_classify.cpp`,
`core/src/consensus/tx_verify.cpp`,
`core/src/consensus/rigor_res.cpp`,
`core/src/wallet/fee_domain.h`.

There are exactly two native assets. Classification is
`AssetTypeFromScriptPubKey(scriptPubKey)`. There is no `(asset, value, script)`
field and no explorer asset ledger on the consensus path.

### ORU

| Role | Fact |
|------|------|
| Transaction asset | Witness v0 key-hash (`WITNESS_V0_KEYHASH`) |
| Fee asset | `NETWORK_FEE_ASSET = ASSET_ORU` |
| Mining subsidy | `GetBlockSubsidy` paid on coinbase `vout[0]` |
| Convergence cost | `RIGOR_MARKER_CONVERGE` outputs are classified **ORU** and are destroyed (not UTXOs) |
| Scale | `COIN = 1e8` (8 decimals) |
| Supply | No absolute cap. Flat **128 ORU** for every `h ≥ 1`. `MAX_MONEY` is a per-value overflow bound (10 billion ORU), not a supply cap. |

### RES

| Role | Fact |
|------|------|
| Reserve asset | Witness v1 32-byte native RES program |
| Issuance | RFC-020 body-tx destroy-to-mint. Same tx destroys ORU (`OP_CONVERGE`) and creates RES |
| Predicate | `R > 0 → R · (2²⁵+h)² ≤ B << 40` (`CheckResMintArithmetic`); `R = 0` unconstrained |
| Scale | `RES_COIN = 1e8` (8 decimals, same as `COIN`) |
| Cap | `RES_MAX_SUPPLY = 419430400000000` atoms (`2²²` RES) |

### PoW does NOT create RES

Confirmed in `rigor_consensus_lock.h` and `res_issuance.h`. A valid PoW
header does not mint RES. GBT `coinbasevalue` is ORU `vout[0]` only.

Fee destroy (RFC-018) is not a UTXO and not a wallet balance: half of the ORU
fee on a RES-transfer tx is unclaimable by coinbase.

---

## 3. Script model

Production has **no Script VM**. `EvalScript` / `VerifyScript` /
`interpreter.cpp` are removed (`core/test/lint/lint-no-script-vm.py`).
Spends go through `VerifyRigorPredicate` → kernel boundary.

Byte namespace: `core/src/script/rigor_bytes.h`. Bytes are classified.
They are not dispatched opcodes.

### Live consensus markers

| Name | Byte | Template | Asset | Spendable |
|------|------|----------|-------|-----------|
| `RIGOR_MARKER_RETURN` | `0x6a` | OP_RETURN / `NULL_DATA`; `nValue` must be 0 | NONE | No |
| `RIGOR_MARKER_CONVERGE` | `0xbd` | `<20-byte HASH160> 0xbd` (22 bytes) | ORU | **No** (not in UTXO set) |

Destroyed: `0xbc` (`RIGOR_BURNED_CVG_CLAIM`, retired `OP_CVG_CLAIM`).
Invalid: `0xbe`–`0xff`.

Legacy Bitcoin opcode values `0x4f`–`0xba` are `RIGOR_RESERVED_LEGACY_*` /
`RIGOR_RESERVED_INERT_*`. Parser-visible, **never executed**.

Removed Bitcoin assumptions:

- No P2PKH / P2SH / P2WSH / bare multisig as creatable monetary scripts
- No Taproot / BIP341 / BIP342
- No secp256k1 / ECDSA spends (Ed25519 only)
- No CLTV/CSV execution
- Witness v1 `CVG2` / `RES2` prefixes are not native RES

### Script-derived classification

| Script shape | `TxoutType` | `AssetType` |
|--------------|-------------|-------------|
| Witness v0, 20-byte program | `WITNESS_V0_KEYHASH` | ORU |
| Witness v1, 32-byte program, not CVG2/RES2 | `WITNESS_V1_RES` | RES |
| Canonical mint encoding | `RES_MINT` | RES |
| 22-byte converge lock | `CONVERGE` | ORU |
| `0x6a` push-only | `NULL_DATA` | NONE |
| Anything else | `NONSTANDARD` | INVALID |

Classifier SSOT: `ClassifyRigorTxout` / `AssetTypeFromScriptPubKey` in
`core/src/script/rigor_classify.cpp`. Do not add a second matcher.

### `OP_CONVERGE` / `RIGOR_MARKER_CONVERGE` behavior

- Destroys ORU (`AddCoins` skips the output)
- Declares the destroy `B` used by `CheckResMintArithmetic`
- Amount ≥ `MIN_CONVERGENCE_AMOUNT` (1 atom). Not a mint price
- Exactly one positive-value converge output per tx
- Only from ORU inputs
- Same tx MAY create Witness v1 RES summing to `R` when the mint inequality holds
- Wallet may create it; wallet cannot spend it

### Mint behavior (RFC-020)

- Ordinary body transaction: ORU inputs, one `OP_CONVERGE` destroy `B`, Witness v1 RES outputs summing to `R`
- `CheckResMintArithmetic(R, B, h)` at the confirming height
- Fractional purchase permitted; display `destroy_cost(h)` is not the construction destroy
- One whole RES costs `ResMinBurnForAtoms(RES_COIN, h)`; one atom costs `ResMinMintBurnAtoms(h)`
- `R = 0` is a legal plain destroy. There is no mint horizon: `destroy_cost(h) > 0` for `h ≥ 0`
- Fractional purchase permitted; display `destroy_cost(h)` is not the construction destroy

---

## 4. Mining model

### Consensus PoW (node)

The live algorithm name is **OpuHash**. OpuHash is SHA3-256 over the
domain-tagged header. `CheckProofOfWork` uses `rigor_kernel::GetRigorPowHash`:

```
OpuHash(header) = SHA3-256("RGRPOW01" || header_80)
valid           ⇔  OpuHash(header) ≤ target
```

Source: `core/src/rigor_kernel/pow_digest.h`, `core/src/pow.cpp`.
Block **id** remains SHA256d of the 80-byte header. Merkle / txid remain
SHA256d.

`GetOpusHash` is defined in `pow_opus.cpp`. It is not OpuHash, and
`CheckProofOfWork` does not call it.

### ASERT

aserti3-2d integer path (`GetNextWorkASERT`):

| Parameter | Mainnet value |
|-----------|----------------|
| Spacing | 600 s |
| Half-life τ | **43200 s** (not 172800) |
| Anchor | height 1 once it exists; genesis while producing height 1 |
| Evaluation | parent time/height only; child `nTime` excluded |
| Per-block clamp | ±12.5% (`875000`–`1125000` ppm) |

### Block template (GBT)

`getblocktemplate` with `{"rules":["segwit"]}`. Node assembles the canonical
coinbase. Required copy-bytes: `default_coinbase_tx`,
`default_coinbase_scriptsig`. Mutable for SOLO: **`time` only**.
`coinbase_address` must be ORU witness v0. RES mint is a body transaction,
not a GBT coinbase prize.

### SOLO

| Mode | Fact |
|------|------|
| SOLO | Miner polls node GBT over HTTP RPC, copies bytes, grinds nonce, `submitblock`. Stratum is refused as a SOLO URL. Default RPC `8761`. |

**Component honesty:** shipped `minerd` grinds the same OpuHash digest as
the node (`SHA3-256("RGRPOW01" || header_80)`). CLI: `-a rgrpow01`
(default). `-a sha256d` is an alias for that same function. The retired
mix in `pow_opus.cpp` is not the grind path.

---

## 5. Issuance model

### ORU subsidy (`GetBlockSubsidy` in `core/src/consensus/rigor_issuance.cpp`)

```
R(0) = 0
R(h) = 128 ORU    for h ≥ 1
```

| Height band | `R(h)` |
|-------------|--------|
| `h = 0` | **0 ORU** |
| `h ≥ 1` | **128 ORU** forever |

There is no Bitcoin 50 COIN fallback and no ORU halving.

### RES mint (`res_issuance.h` / RFC-020)

```
destroy_cost(h) = P0 · (2²⁵ + h)² >> 50          // display only
R > 0  →  R · (2²⁵ + h)² ≤ B << 40
R = 0  →  B unconstrained
RES_MAX_SUPPLY = 419430400000000 atoms    // 2²² RES = (128/1024)·2²⁵
```

`P0 = destroy_cost(0) = 102400000000` (1024 ORU per 1 RES). `RES_COIN = 100000000`.
Numeric SSOT: `tools/res_continuous_price_schedule.py`.

### Convergence / destroy

| Parameter | Value |
|-----------|-------|
| `MIN_CONVERGENCE_AMOUNT` | 1 atom (live destroy floor; not a mint price) |
| One-atom mint floor | `ResMinMintBurnAtoms(h)` ORU atoms (1024 at height 0, 1025 at height 1) |
| Whole-RES destroy | `ResMinBurnForAtoms(RES_COIN, h)`; do not feed display `destroy_cost(h)` back as `B` |
| One-atom mint floor | `ResMinMintBurnAtoms(h)` ORU atoms |

### Maturity

Mainnet / testnet / regtest / devtest: `nCoinbaseMaturity = 100`.
Applies to ORU coinbase and RES mint UTXOs.

### RFC-018 fee destroy

`OruResTransferFeeBurnShare(fee) = floor(fee / 2)` on RES-transfer txs.
Pure ORU, Convergence, and the ORU subsidy formula are unaffected.

---

## 6. Wallet (identity only)

- Descriptor SQLite wallet; `WALLET_SCHEMA_VERSION = 0`
- Dual asset UI/RPC: `getbalances.oru` and `getbalances.res`
- ORU addresses: Bech32 `rg1q…` (witness v0)
- RES addresses: Bech32m witness v1
- POOL-T transfers never mix assets in one tx
- POOL-F fees are ORU-only
- Wallet constructs Convergence / mint txs; consensus checks `CheckResMintArithmetic`
- Wallet does not mine

---

## 7. Consensus lock files

### `core/src/consensus/params.h`

Holds `Consensus::Params` (genesis hash, subsidy fields, buried deployments,
ASERT/PoW fields, maturity).

### `core/src/consensus/res_issuance.h`

RFC-020 continuous `destroy_cost(h)`, pole `CheckResMintArithmetic`, cap `2²²` RES.
Numeric SSOT with `tools/res_continuous_price_schedule.py`.

### `core/src/consensus/rigor_consensus_lock.h`

`RIGOR_CONSENSUS_LOCK_VERSION = 3`. Pins:

- `CHAIN_NAME = "RIGOR"`
- `BECH32_HRP = "rg"`
- Block size/weight 1_000_000; sigops cost 20_000
- ORU subsidy **128** for `h ≥ 1`, genesis 0
- `RES_COIN = 1e8`, `RES_MAX_SUPPLY = 419430400000000` (`2²²` RES)
- `RES_DESTROY_POLE = 2²⁵`, `RES_DESTROY_SHIFT = 40`
- RFC-018 1/2 RES-transfer fee destroy
- Retired mix **parameter** lock (`OPUS_POW_*` in `pow_opus.cpp`; not live OpuHash)
- Mainnet ASERT τ = 43200

---

## 8. Documentation errors this extraction replaces

| Stale claim | Current code |
|-------------|--------------|
| Historical `v0.1.1` display channel | Display version in `core/src/clientversion.h` (`RIGOR_RELEASE_VERSION`) |
| `GetOpusHash` is the mining digest | OpuHash is FIPS SHA3-256 `"RGRPOW01" \|\| header_80`, via `GetRigorPowHash` |
| ASERT half-life 172800 s | **43200 s** |
| `R(0) = 1000 ORU` | `R(0) = 0` |
| `IsConvergeScript` in `interpreter.cpp` | `rigor_classify.cpp` / `predicate_verify.h`; no VM |
| RES cap / mint | RFC-020 cap `2²²` RES / pole mint / `RES_COIN = 1e8` |
| Bitcoin 50 COIN subsidy fallback | Fail-closed zero if base/interval misconfigured |
| Signet as a shipped network | Not implemented |
| SPEC-008 still “PROPOSED only” as if unimplemented | Node already validates SHA3-256 |

Retired mix parameters are not live OpuHash. Live OpuHash is SHA3-256 / `RGRPOW01`.
