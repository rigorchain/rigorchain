# SPEC-000: RIGOR Consensus Specification

**Status:** CURRENT PRODUCTION (protocol description)  
**Protocol SSOT:** [`../protocol/RFC-020.md`](../protocol/RFC-020.md),
[`../protocol/RFC-018.md`](../protocol/RFC-018.md)  
**Implementation:** `core/src/consensus/`, `core/src/script/`, `core/src/pow.cpp`,
`core/src/rigor_kernel/pow_digest.h` (realization)  
**Lock files:** `core/src/consensus/params.h`,
`core/src/consensus/rigor_consensus_lock.h`

```
equivalence_claimed = false
documentation_consensus_authority = false
```

Protocol specifications are normative SSOT.
Implementations realize the frozen protocol.
If this file disagrees with RFC-020 / RFC-018 on their scope, the RFC is correct.
If this file disagrees with `core/src/`, reconcile both against the live RFC.
The implementation does not define the protocol.

This document describes production consensus. It is not a second money
formula. Live RFC texts are `docs/protocol/RFC-018.md` and
`docs/protocol/RFC-020.md`. Earlier RFC texts are not protocol authority.

---

## 1. Scope

### 1.1 Networks

Shipped consensus networks (`core/src/kernel/chainparams.cpp`):

| Network | Chain id | P2P | RPC | HRP |
|---------|----------|-----|-----|-----|
| main | `main` | 8760 | 8761 | `rg` |
| testnet | `test` | 18760 | 18761 | `trg` |
| regtest | `regtest` | 28760 | 28761 | `rgrt` |
| devtest | `devtest` | 48760 | 48761 | `dvt` |
| slowtest | `slowtest` | 49760 | 49761 | `slt` |

There is no `CSignetParams`. Signet is not a shipped network.

Mainnet genesis hash:
`e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468`.

The same consensus predicates apply on every shipped network. Only
genesis, magic, ports, HRP, and network-scoped timing constants differ.

### 1.2 ORU

**ORU** is the operational asset. It enters circulation through coinbase
subsidy (`GetBlockSubsidy`) and leaves circulation through Convergence
destroys and the RFC-018 fee-destroy share.

- Scale: `COIN = 1e8`
- Spendable path: Witness v0 / 20-byte program
- Convergence destroy: `<20-byte HASH160> 0xbd` (classified ORU, not a UTXO)
- Conservation on non-coinbase txs: inputs = outputs + claimable fee
- No absolute supply cap. `MAX_MONEY` is a per-value overflow bound.

Subsidy (`core/src/consensus/rigor_issuance.cpp`):

```
R(0) = 0
R(h) = 128 ORU    for h ≥ 1
```

### 1.3 RES

**RES** is minted in a body transaction that destroys ORU at the confirming
height (RFC-020).

- Scale: `RES_COIN = 1e8` (same as `COIN`)
- Cap: `RES_MAX_SUPPLY = 419_430_400_000_000` atoms (`2²²` RES)
- Spendable path: Witness v1 / 32-byte Ed25519 program
- Issuance: body tx with one `OP_CONVERGE` destroy `B` and RES outputs summing to `R`
- Predicate: `R > 0 → R · (2²⁵+h)² ≤ B << 40`
- Genesis destroy cost: `1024 · COIN`
- `R = 0` is a legal plain destroy

```
S_RES(0) = 0
S_RES(h+1) = S_RES(h) + body_mint_atoms(h+1)
S_RES(h) ≤ 419_430_400_000_000
```

### 1.4 Proof-of-Work

**OpuHash** is the live work function:

```
OpuHash(header) = SHA3-256("RGRPOW01" || header_80)
valid           ⇔  OpuHash(header) ≤ ExpandCompact(nBits)
```

Source: `rigor_kernel::GetRigorPowHash`, `CheckProofOfWork`.

- Block **id** remains `SHA256d(header_80)`
- Genesis is PoW-exempt after a valid `nBits`
- ASERT (aserti3-2d): spacing 600 s; mainnet half-life **43200 s**;
  anchor height 1; per-block clamp ±12.5%
- `GetOpusHash` in `pow_opus.cpp` is not OpuHash. `CheckProofOfWork` does not call it
- The mint price is not `nBits`

### 1.5 Dual-asset model

| Asset | Role | Creation |
|-------|------|----------|
| ORU | Fees, miner subsidy, Convergence weight | Coinbase subsidy |
| RES | Scarce reserve | Body-tx destroy-to-mint at `destroy_cost(h)` |

Asset class is `AssetTypeFromScriptPubKey(scriptPubKey)`. There is no
`(asset, value, script)` field on `CTxOut`.

### 1.6 Locked parameters

| Item | Value |
|------|-------|
| `RES_COIN` | `100_000_000` |
| `RES_DESTROY_COST_GENESIS` | `1024 * COIN` = `102_400_000_000` |
| `RES_DESTROY_POLE` | `2²⁵` blocks |
| `RES_DESTROY_SHIFT` | `40` |
| Mint predicate | `R > 0 → R · (2²⁵+h)² ≤ B << 40` |
| `RES_MAX_SUPPLY` | `419_430_400_000_000` atoms (`2²²` RES) |
| ORU subsidy `h ≥ 1` | 128 ORU |
| Fee destroy | `floor(fee/2)` on RES-transfer ORU fees only |
| Activation | Genesis-only |

---

## 2. System model

### 2.1 Chain

A chain is a finite sequence of blocks from genesis. The active chain is
the valid chain with greatest cumulative chainwork. Ties break
deterministically.

### 2.2 Block

A block is a header plus an ordered transaction list. Validity is the
conjunction of header, OpuHash, transaction, mint, supply, and witness
canonicality predicates.

Coinbase scriptSig:

```
<BIP34 height>
```

Coinbase scriptSig is the BIP34 height push only.

### 2.3 Transaction

Each output is `(nValue, scriptPubKey)`. **txid** is SHA256d of the
canonical non-witness serialization.

There is **no Script VM**. Bytes are classified; spends go through
`VerifyRigorPredicate`.

Live templates:

| Template | Encoding | Asset | Spendable |
|----------|----------|-------|-----------|
| ORU P2WPKH | Witness v0, 20-byte | ORU | Yes |
| RES v1 | Witness v1, 32-byte (not CVG2/RES2) | RES | Yes |
| RES v1 mint output | Witness v1, 32-byte | RES | After maturity (100) |
| Converge | `<HASH160> 0xbd` (22 bytes) | ORU | No (not a UTXO) |
| NULL_DATA | `0x6a` + pushes, `nValue = 0` | NONE | No |

### 2.4 UTXO

The UTXO set maps `(txid, index)` to `(nValue, scriptPubKey)`.
Convergence outputs are not inserted. Account balances are not consensus
truth.

### 2.5 ORU circulation (observational)

Per-block changes:

- `+ R(h+1)` claimable coinbase subsidy
- `+` claimable body ORU fees (all ORU fees minus the RFC-018 destroy share)
- `− C(h+1)` ORU destroyed by valid Converge outputs
- `−` RFC-018 destroy share of RES-transfer ORU fees
- `−` unclaimed remainder of the coinbase ORU limit

`S_ORU(h+1) = S_ORU(h) + R(h+1) − C(h+1)` is an issued-minus-converge
identity. It is not circulating UTXO ORU once fee destroys or unclaimed
coinbase occur. `ConnectBlock` must not reject a block by comparing
those two figures.

### 2.6 Consensus state

```
S(h) = ( ChainTip, UTXO, S_ORU, S_RES, PoWState, ValidationCursor )
```

Mempool, wallet labels, and explorer indices are excluded.
Mint authorization is a function of the confirming block’s own body
transactions.

---

## 3. Consensus objectives

These properties are what the production predicates are written to
enforce. `equivalence_claimed` remains false.

### CO-1: Chain validity

The active chain is a well-formed sequence of valid blocks from genesis,
selected by greatest chainwork with a deterministic tie-break.

### CO-2: Deterministic block acceptance

For fixed `S(h)` and candidate `B(h+1)`, every correct validator reaches
the same accept-or-reject verdict.

### CO-3: Deterministic state transition

Accepted `B(h+1)` yields a unique `S(h+1)`.

### CO-4: Supply integrity

ORU and RES change only on the subsidy, destroy, fee-destroy, and body-mint
paths. `S_RES(h) ≤ 419_430_400_000_000`.

### CO-5: Replay determinism

Replaying `B(0..h)` yields the same `S(h)`.

### CO-6: Reorg determinism

A reorg yields the same state as sequential application of the winning
chain.

### CO-7: Locked constants

Validators enforce the table in §1.6. The mint price is not derived from
PoW difficulty.

---

## 4. Assumptions

- **A-NET-1:** Validators eventually see the same active-chain history.
  Synchrony is not required for correctness.
- **A-NET-2:** Relay, discovery, and ban scoring are non-consensus.
- **A-HASH-1:** `Hash256(x) = SHA256(SHA256(x))` is deterministic.
- **A-HASH-2:** OpuHash (`SHA3-256` / `RGRPOW01`) is deterministic.
- **A-HASH-3:** Hash256 and OpuHash digests are not asset values.
- **A-TIME-1:** Header times enter ASERT and MTP. Wall-clock is not a
  consensus input.
- **A-NODE-1:** Validation is a function of chain history and the
  candidate block.
- **A-NODE-2:** Caches must not feed back into predicates.
- **A-NODE-3:** Mint arithmetic is integer only (`R · (2²⁵+h)² ≤ B << 40`).

---

## 5. Trust anchors

Accepted without a machine-checked refinement proof:

| Anchor | Role | Formal status |
|--------|------|---------------|
| SHA-256 / Hash256 | txid, merkle | Not proven equivalent to FIPS 180-4 in Coq |
| SHA3-256 / OpuHash | Header work | Coq has no PoW module |
| 128-bit mint product | `CheckResMintArithmetic` | Overflow bound not proven |
| Golden vectors | Finite byte oracle (433 / 226) | Authoritative for recorded bytes, not for intent |

```
equivalence_claimed = false
hash_kernel_shared_binary_with_cpp = false
refinement_proved = false
```

---

## 6. Non-goals

| Non-goal | Production fact |
|----------|-----------------|
| Script VM | No `EvalScript` / `interpreter.cpp` |
| Smart contracts | No general on-chain computation |
| P2WSH / P2PKH / P2SH / Taproot | Not creatable |
| Proof-of-Stake | Fork choice is PoW / chainwork |
| Governance keys | None |
| Account ledger | UTXO only |
| Explorer / wallet authority | Observational |
| Signet | Not implemented |

---

## 7. Formal verification

```
  PROTOCOL (RFC-020 / RFC-018 / this specification)
        │
  IMPLEMENTATION (core/src/ — realization)
        │
  EVIDENCE (tests, golden vectors)
        │
  FORMAL MODEL (Coq, executable oracle — model properties)
```

The living lock is **433** shared golden vectors and **226** Coq-enforced
vectors (`formal_verification/golden_lock.json`). That is finite
correspondence, not refinement.

Protocol specifications are normative SSOT.
Implementations realize the frozen protocol.
When documentation disagrees with the live RFC, update the document.

Related: [SPEC-008 Proof-of-Work](./SPEC-008-PROOF-OF-WORK.md),
[SPEC-003 Consensus Rules](./SPEC-003-CONSENSUS-RULES.md),
[SPEC-005 Consensus Invariants](./SPEC-005-CONSENSUS-INVARIANTS.md).

---

## 8. Source index

| Topic | Path |
|-------|------|
| Consensus params | `core/src/consensus/params.h` |
| Locks / subsidy / mint | `core/src/consensus/rigor_consensus_lock.h` |
| RES destroy cost | `core/src/consensus/res_issuance.h` |
| ORU subsidy | `core/src/consensus/rigor_issuance.cpp` |
| Classification | `core/src/script/rigor_classify.cpp` |
| Spends | `core/src/script/predicate_verify.cpp` |
| Tx / mint locus | `core/src/consensus/tx_check.cpp` |
| Convergence | `core/src/consensus/convergenceres.cpp`, `core/src/consensus/tx_verify.cpp` |
| OpuHash | `core/src/rigor_kernel/pow_digest.cpp` |
| PoW / ASERT | `core/src/pow.cpp` |
| Extracted facts | `docs/developer/CODE_SSOT_PROTOCOL_FACTS.md` |
