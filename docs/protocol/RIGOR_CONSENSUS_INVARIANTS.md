# RIGOR Consensus Invariants

**Status:** Extraction of frozen RIGOR Mainnet consensus properties.

This document describes protocol invariants already defined by the frozen RIGOR consensus design.

It is not a specification change.
It introduces no consensus rule, governance mechanism, activation procedure, or upgrade authority.

**Authority model:**

```
Protocol definition
        ↓
Consensus invariants
        ↓
Reference implementation
        ↓
Running software
```

The protocol defines the rules.
This document records those frozen properties.
The production implementation realizes them.

If implementation behavior diverges from the frozen protocol, the implementation contains the defect. The defect does not redefine the protocol. The correction brings the implementation back into conformance with the frozen protocol.

If this document misstates a frozen property, the document is corrected. An incorrect implementation is not adopted as a new protocol definition.

Documentation and implementation must be reviewed against the frozen protocol definition.

```
new_consensus_rules = false
governance_layer = false
upgrade_activation = false
documentation_consensus_authority = false
```

---

## 1. Protocol Permanence Principle

RIGOR is designed as a fixed monetary protocol.

After mainnet activation, the network does not contain any consensus mechanism that can modify:

- monetary issuance rules;
- asset identity rules;
- transaction validity rules;
- Proof-of-Work identity;
- ledger model;
- genesis identity.

There is:

- no governance layer;
- no voting mechanism;
- no version-bit monetary upgrade path;
- no emergency consensus override.

A change to a consensus invariant is not a RIGOR software upgrade.

It is a different protocol.

A network running modified consensus rules is a separate blockchain with separate monetary properties.

---

## 2. Consensus Boundary

The RIGOR protocol boundary is defined by four invariant categories.

```
Monetary Invariants
        +
Ledger Invariants
        +
Execution Invariants
        +
Network Identity Invariants
```

Together they define the meaning of the chain.

---

## Protocol vs Implementation Boundary

Consensus-critical code does not define the protocol.

The protocol defines the rules that determine valid state transitions.

Reference implementation code is only the execution mechanism that realizes those rules.

A function, file, test, or code path may be consensus-critical because it implements a protocol property, but it does not become a source of authority.

The authority relationship remains:

```
Protocol Definition
        ↓
Consensus Invariants
        ↓
Reference Implementation
        ↓
Running Software
```

---

## 3. Monetary Invariants

Monetary rules define what assets exist, how they enter circulation, and how scarcity is enforced.

These rules are consensus-critical.

### 3.1 ORU Issuance

ORU is created only through valid Proof-of-Work subsidy.

Consensus rule:

```
subsidy(h) = 0          if h <= 0
subsidy(h) = 128 ORU    if h >= 1
```

`128 ORU` is `ORU_FLAT_BLOCK_SUBSIDY` = `128 * COIN` = `12800000000` atoms. `COIN` is `100000000`. Height 0 uses `ORU_GENESIS_BLOCK_SUBSIDY` = `0`. `GetBlockSubsidy` ignores chain parameters. `RES_REWARD_EPOCH` (`210000`) is an observational calendar. It does not cut the subsidy.

Properties:

- no halving;
- no administrative issuance;
- no founder allocation;
- no treasury issuance.

The miner subsidy is independent of:

- transaction volume;
- RES creation;
- holder behavior.

A coinbase may also collect ORU fees already present in the UTXO set. That collection is not new issuance. The claimable coinbase ORU is bounded by:

```
claimable = (ORU fees − RES-transfer fee burn) + subsidy(h)
```

The RES-transfer fee burn is `floor(fee / 2)`. `RigorSupplyDelta` rejects a larger coinbase with `bad-cb-amount`. Fees, the burn, and the subsidy are the whole coinbase ORU budget.

The subsidy rule is enforced by consensus validation.

Changing:

```
128 ORU/block
```

creates a different monetary protocol.

### 3.2 RES Supply Boundary

RES has a protocol-defined maximum supply:

```
RES_MAX_SUPPLY = 2^22 RES
               = 4,194,304 RES
               = 419,430,400,000,000 atoms
```

This value is derived from the issuance function:

```
S  = 128 ORU
P0 = 1024 ORU

RES_MAX_SUPPLY = (S / P0) × 2^25
               = 2^22 RES
```

That product is the integral of the real-valued ceiling `S / destroy_cost(h)` from height 0 to infinity. It is not the sum of the integer per-block maxima, and it is not an amount the chain can mint.

`P0` is `RES_DESTROY_COST_GENESIS`: the ORU destroyed to mint one whole RES at height 0. The pole `2^25` is `RES_DESTROY_POLE`. The consensus mint check does not use a displayed price. It evaluates:

```
R = 0  →  a plain ORU destroy is allowed
R > 0  →  R · (2^25 + h)^2  ≤  B << 40
```

`R` is RES atoms created by the transaction. `B` is ORU atoms destroyed by that transaction's Convergence output. `40` is `RES_DESTROY_SHIFT`, forced by `P0 = 1024 ORU`. A single mint with `R` above `RES_MAX_SUPPLY` is rejected.

RES cannot be:

- mined;
- created by coinbase;
- issued by an administrator;
- created outside the convergence rule.

`GetBlockSubsidy` returns ORU only. Mainnet coinbase RES must be 0 (`bad-cb-res-amount`). A body transaction creates RES only by destroying ORU in the same transaction. The predicate is a pure function of `(R, B, h)`. No key is a mint authority.

A single transaction with `R` above `RES_MAX_SUPPLY` is rejected. Block validation does not keep a cumulative RES counter. Height 0 creates no ORU. ORU created at height `k ≥ 1` can be destroyed only at a height `≥ k`. Along the schedule that destroys each subsidy at the height it is created, `Σ_{h≥1} (128·10^8 · 2^40) / (2^25 + h)^2` is strictly below `128·10^8 · 2^40 / 2^25 = 2^22` RES, because `Σ_{k=2^25+1}^{∞} 1/k^2 < 2^{-25}`. The integer predicate admits only the floor of each term, so the admitted total is strictly below `RES_MAX_SUPPLY`. The constant is that upper bound. It is not an amount the chain can mint.

Changing:

```
RES_MAX_SUPPLY
```

creates a different monetary protocol.

### 3.3 Asset Direction Invariant

RIGOR defines a one-way monetary transformation:

```
ORU → RES
```

A valid transition exists:

```
destroy ORU
    |
    v
create RES
```

The reverse transition does not exist:

```
RES → ORU
```

No consensus rule allows:

- RES redemption;
- RES conversion;
- administrative conversion.

An ORU output funded without ORU inputs is rejected (`illegal-res-to-oru-conversion`). RES may be spent to RES. The network fee on that spend is still ORU. RES amounts never pay the fee.

Changing this direction changes the meaning of the two-asset architecture.

---

## 4. Ledger Invariants

RIGOR uses a single UTXO state model.

Consensus recognizes:

```
one chain
one UTXO set
one validation engine
two native asset identities
```

The protocol does not contain:

- account balances;
- contract storage;
- external asset registry;
- parallel asset ledger.

Asset identity is derived from:

```
scriptPubKey
    +
consensus classification
```

Classification is `AssetTypeFromScriptPubKey`. There is no `(asset, amount, script)` consensus field.

It does not come from:

- wallet software;
- explorer databases;
- external metadata.

`AddCoins` inserts an output only when it is neither Convergence nor `OP_RETURN`. Convergence value leaves circulation because the output is never stored. Spending a missing or already-spent outpoint fails closed. Wallet balances and observational supply ledgers are views. They are not a second ledger.

Changing the ledger model creates a different protocol.

---

## 5. Execution Invariants

Execution rules define how transactions are authorized and how outputs are classified.

### 5.1 Script Templates

RIGOR accepts only the defined consensus templates:

| Template | Asset | Meaning |
|---|---|---|
| Witness v0 / 20 bytes | ORU | spendable ORU |
| Witness v1 / 32 bytes Ed25519 | RES | spendable RES |
| HASH160 + `0xbd` | ORU destroy | Convergence |
| zero-value `OP_RETURN` | data | non-asset data |

Witness v0 is ORU only at program length 20. Witness version is `0x00`. Witness v1 is RES only at program length 32, witness version `0x51`, and only when the program does not begin with the retired prefixes `CVG2` or `RES2`. The 32-byte program is the Ed25519 public key.

Convergence is the 22-byte script `PUSHBYTES_20 || 20-byte HASH160 || 0xbd`. `0xbd` means this ORU output is destroyed. Opcode meaning is consensus meaning. Changing `OP_CONVERGE` semantics changes the monetary protocol.

`OP_RETURN` is byte `0x6a` followed by pushes. Its value must be 0. A non-zero value is not a burn and not a third asset.

Byte `0xbb` is recognized when the script is the canonical encoding `<CScriptNum height> <20-byte hash> 0xbb`. `ClassifyRigorTxout` returns `TxoutType::RES_MINT` for that encoding, and `AssetTypeFromScriptPubKey` returns `ASSET_RES`, so `IsAllowedScript` does not reject the byte by itself. Recognition is not acceptance. `CheckTransaction` rejects a non-coinbase output with that script (`bad-txns-res-mint-noncoinbase`). On mainnet a coinbase whose RES output value is not 0 is rejected with `bad-cb-res-amount`. `0xbb` does not create RES.

The protocol does not contain:

- Script VM;
- arbitrary opcode execution;
- smart contracts.

Legacy opcode numbers, including `0xac` (`CHECKSIG`), are inert. They do not authorize a spend. Bare P2PKH, P2SH, P2WSH, and Taproot are not consensus templates.

Changing accepted templates changes the transaction language of RIGOR.

### 5.2 Signature Rules

RIGOR transaction authorization uses:

```
Ed25519
```

Consensus parameters:

```
Public key:   32 bytes
Signature:    64 bytes
Sighash:      RIGOR domain-separated signing
```

The signature is a raw detached Ed25519 signature. There is no hashtype byte. ORU spends commit under the domain `RGRORU01`. RES spends commit under `RGRRES01`. The sighash covers the domain tag, the chain id (the genesis hash), and the whole transaction.

The authorization path is fixed by:

```
VerifyRigorPredicate
```

`ConnectBlock` reaches that function through `CheckInputScripts`. A witness program without witness verification fails closed. It is not anyone-can-spend. RES has one key path. ECDSA and secp256k1 are not authorization rules.

Changing:

- signature algorithm;
- key format;
- signing domain;
- witness authorization rules;

changes which transactions are valid.

Therefore it changes the protocol.

### 5.3 Proof-of-Work Identity

RIGOR separates:

```
block identity
```

from

```
Proof-of-Work validity
```

Block identity:

```
SHA256d(header_80)
```

Proof-of-Work:

```
SHA3-256("RGRPOW01" || header_80)
```

The header is 80 bytes: version, previous block, merkle root, time, `nBits`, nonce. The domain prefix is 8 bytes. The preimage is 88 bytes. A header is valid when its block id is the genesis hash, or when the SHA3-256 digest is at or below the `nBits` target. SHA3-256 of the header without `RGRPOW01` is not a proof.

Consensus-critical properties include:

- 80-byte header layout;
- `RGRPOW01` domain separation;
- SHA3-256 PoW;
- target calculation;
- ASERT parameters.

Mainnet target spacing is 600 seconds. The ASERT half-life is 43200 seconds, anchored at height 1, with a per-block clamp of ±12.5%. Mainnet genesis `nBits` is `0x1d008000`.

Changing Proof-of-Work creates a different chain.

---

## 6. Network Identity Invariants

Genesis defines chain identity.

Mainnet identity:

```
Genesis hash:

e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468

Genesis activation:

2026-10-25 06:00:00 UTC
```

That instant is `nTime = 1792908000`. The same header also locks:

| Field | Value |
|---|---|
| Merkle root | `9d3fbd207af927fc8e2c57e975d8d5845913bf059d956592e8a0bcf38b3515dd` |
| `nBits` | `0x1d008000` |
| `nNonce` | `0` |
| `nVersion` | `1` |
| Coinbase value | `0` |
| Coinbase message | `Selection not computation. Convergence defines state. Naming is consensus.` |

Genesis parameters are not configuration data.

They identify the network.

A different genesis identity represents a different blockchain.

---

## 7. Consensus Change Classification

### Consensus-preserving changes

These do not modify which blocks or transactions are valid:

- explorer improvements;
- wallet interface improvements;
- RPC additions;
- monitoring tools;
- documentation updates.

### Protocol-breaking changes

The following create a different protocol.

**Monetary**

- changing ORU issuance;
- adding halving;
- changing RES supply;
- changing convergence rules;
- adding administrative issuance.

**Asset model**

- adding new asset classes;
- changing the ORU/RES relationship;
- adding reverse conversion.

**Transaction model**

- adding new script templates;
- changing opcode meaning;
- replacing Ed25519 authorization.

**Chain security**

- changing the PoW algorithm;
- changing header structure;
- changing difficulty rules.

**Identity**

- changing genesis parameters.

A protocol-breaking change is not a RIGOR upgrade. It creates a separate network.

---

## 8. No Consensus Upgrade Mechanism

RIGOR intentionally does not include a mechanism that changes these invariants.

There is:

- no on-chain governance;
- no consensus voting;
- no version-bit monetary activation;
- no emergency override.

The only compiled version-bits deployment is `DEPLOYMENT_TESTDUMMY`, and on mainnet its start time is `NEVER_ACTIVE`. It does not retarget issuance, assets, scripts, signatures, or proof-of-work.

A future system may choose different rules.

However:

A system with different consensus invariants is not an upgraded RIGOR Mainnet.

It is a separate protocol.

---

## 9. Verification Philosophy

The purpose of these invariants is not to create trust in a development team.

The purpose is to define what independent participants can verify.

A user does not need to trust:

- a website;
- an explorer;
- a developer statement.

A full node evaluates:

- issuance;
- asset classification;
- transaction validity;
- Proof-of-Work;
- state transition rules.

The frozen protocol is the boundary. The reference implementation realizes that boundary in consensus code.

[`core/src/test/consensus_invariants_tests.cpp`](../../core/src/test/consensus_invariants_tests.cpp) pins the constants and the validation paths named in this document. A divergence from those recorded properties fails that suite. The suite does not add a rule, and it does not redefine the protocol. It checks that the reference implementation still realizes the frozen properties this document records.

### How to run the suite

From the repository root:

```bash
core/src/test/test_rigor --run_test=consensus_invariants_tests
```

A passing run prints `Running 14 test cases...` and ends with `*** No errors detected`.

The 14 cases are one suite. Section 10 lists fewer rows because that table names enforcement files, and several cases call the same file. The directories `core/src/consensus/`, `core/src/kernel/`, and `core/src/script/` are where those files live.

| Test | Rule it pins | Source |
|---|---|---|
| `oru_subsidy_is_flat_and_has_no_halving` | Height 0 pays 0. Every height ≥ 1 pays 128 ORU. Height 210000 does not halve. | [`rigor_issuance.cpp`](../../core/src/consensus/rigor_issuance.cpp) |
| `coinbase_cannot_mint_oru_or_res` | A coinbase cannot claim ORU beyond the subsidy bound. Mainnet coinbase cannot carry RES. | [`rigor_supply.cpp`](../../core/src/consensus/rigor_supply.cpp) |
| `res_max_supply_and_destroy_cost_are_closed_form` | `RES_MAX_SUPPLY = 2^22` RES, genesis destroy cost 1024 ORU, pole `2^25`, shift 40. | [`res_issuance.h`](../../core/src/consensus/res_issuance.h) |
| `res_mint_predicate_rejects_underpay_and_free_mint` | One atom short mints nothing. A zero burn cannot mint. One mint cannot exceed the cap. | [`res_issuance.h`](../../core/src/consensus/res_issuance.h) |
| `body_mint_requires_converged_oru` | A body transaction mints RES only by destroying enough ORU through `OP_CONVERGE`. | [`tx_verify.cpp`](../../core/src/consensus/tx_verify.cpp) |
| `res_cannot_convert_to_oru` | RES inputs alone cannot create ORU (`illegal-res-to-oru-conversion`). | [`tx_verify.cpp`](../../core/src/consensus/tx_verify.cpp) |
| `script_templates_are_closed` | The four templates classify to their assets. P2WSH, P2PKH, `CVG2`, and `RES2` are rejected. | [`rigor_classify.cpp`](../../core/src/script/rigor_classify.cpp) |
| `only_listed_templates_pass_checktransaction` | `CheckTransaction` accepts those templates. `OP_RETURN` value must be 0. | [`tx_check.cpp`](../../core/src/consensus/tx_check.cpp) |
| `opcode_bytes_keep_consensus_meaning` | `0x6a` is RETURN. `0xbd` is the Convergence marker. `0xbb` is classified as a mint-shaped script and does not create RES (`bad-txns-res-mint-noncoinbase` on a body transaction; mainnet coinbase RES other than 0 is `bad-cb-res-amount`). `0xbc` is burned. `0xac` is inert and is not `CHECKSIG`. | [`rigor_bytes.h`](../../core/src/script/rigor_bytes.h), [`tx_check.cpp`](../../core/src/consensus/tx_check.cpp) |
| `authorization_is_ed25519_only` | Public key is 32 bytes. Signature is 64 bytes. Domains are `RGRORU01` and `RGRRES01`. A 33-byte secp public key is rejected. | [`predicate_verify.cpp`](../../core/src/script/predicate_verify.cpp) |
| `ledger_is_utxo_admission` | ORU and RES enter the UTXO set. Convergence and `OP_RETURN` do not. | [`coins.cpp`](../../core/src/coins.cpp) |
| `pow_is_sha3_rgrpow01` | Domain `RGRPOW01`, 80-byte header, SHA3-256 digest, and `CheckProofOfWork`. | [`pow_digest.cpp`](../../core/src/rigor_kernel/pow_digest.cpp), [`pow.cpp`](../../core/src/pow.cpp) |
| `mainnet_genesis_identity` | Mainnet genesis hash, merkle root, time, `nBits`, and nonce. | [`chainparams.cpp`](../../core/src/kernel/chainparams.cpp) |
| `no_versionbits_activation` | The only version-bits deployment is `TESTDUMMY`, and it is `NEVER_ACTIVE`. | [`chainparams.cpp`](../../core/src/kernel/chainparams.cpp) |

---

## 10. Reference Implementation

These locations are where the production code realizes the frozen properties. They are not a separate protocol authority.

Consensus realization locations:

```
core/src/consensus/
core/src/kernel/
core/src/script/
core/src/validation.cpp
core/src/coins.cpp
```

| Component | Function |
|---|---|
| `rigor_issuance.cpp` | ORU issuance |
| `res_issuance.h` | RES supply rules |
| `rigor_supply.cpp` | Coinbase ORU bound and the rejection of coinbase RES |
| `tx_verify.cpp` | RES creation rules and the rejection of RES → ORU |
| `tx_check.cpp` | Accepted output templates |
| `rigor_classify.cpp` | Asset classification |
| `predicate_verify.cpp` | Authorization (`VerifyRigorPredicate`) |
| `pow_digest.cpp` | Proof-of-Work digest |
| `pow.cpp` | Proof-of-Work check |
| `chainparams.cpp` | Genesis identity |
| `coins.cpp` | UTXO admission |

Wallet, RPC, and explorer code may refuse a transaction that consensus would also refuse. They do not define these invariants.

---

## Final Statement

RIGOR does not define value through promises.

It defines a protocol boundary through deterministic rules.

The purpose of consensus invariants is:

```
to make monetary rules inspectable
to make asset rules deterministic
to make protocol boundaries explicit
```

A participant may evaluate the design.

A developer may improve software around it.

But changing the consensus boundary means creating a different protocol.

RIGOR Mainnet remains the frozen protocol. Every node independently verifies whether the software it runs realizes those rules.
