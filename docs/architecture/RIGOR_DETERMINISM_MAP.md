# RIGOR Data Flow & Determinism Map

**Document status:** Production specification

Issuance is RFC-020. ORU subsidy is 0 at height 0 and 128 ORU at every height ≥ 1. RES is minted only by a body transaction that destroys ORU. The consensus predicate is `R · (2²⁵ + h)² ≤ B << 40`, with `R` in RES atoms, `B` in ORU atoms, and `h` the confirming height. `tools/res_continuous_price_schedule.py` prints the same parameters and the integer helper. Its `rho * RES_MAX_SUPPLY` line is not an on-chain total.

```
equivalence_claimed = false
```

This document maps data flows and the determinism model: the state transition, the absence of global mutable consensus state, replay of accepted blocks, hash roles, and Convergence.

---

## 1. Determinism Definition

RIGOR consensus satisfies:

```
∀ block B at height h with parent context P:
  evaluate(B, P) → (accept | reject, state_delta)
```

For an accepted block, the UTXO set produced by `ConnectBlock` is identical across platforms, compilers of the same source revision, and `rigord` processes. Same block bytes and the same parent UTXO view produce the same validity decision.

Explorer databases, wallet views, and pool telemetry are not that decision.

---

## 2. State Transition Determinism Rules

### 2.1 Block-Level Transition Function

```
T(S_{h-1}, B_h) → S_h
```

| Component | Deterministic source |
|-----------|---------------------|
| UTXO set | `UpdateCoins` after per-transaction validation. A `0xbd` output is not inserted |
| ORU subsidy | `GetBlockSubsidy`: 0 at height 0, 128 ORU thereafter |
| ORU ledger scalar | Previous scalar + subsidy − ORU destroyed by Convergence in this block. RFC-018 fee destruction is not part of this scalar |
| RES supply | Previous total + RES atoms minted by body transactions that pass `CheckResMintArithmetic` |
| Coinbase ORU | At most `(ORU fees − RES-transfer fee burn) + subsidy`. A larger coinbase is `bad-cb-amount` |
| Coinbase RES | 0 on mainnet. Any other amount is `bad-cb-res-amount` |

Inputs frozen at evaluation time:

- The block's transaction list, in order
- The parent chain back to genesis
- The UTXO view at block entry
- The confirming height `h` used by the mint inequality

### 2.2 Per-Transaction Determinism

For each transaction:

```
1. Classify each output with AssetTypeFromScriptPubKey
2. Reject a script that is not an allowed template
3. Reject a non-coinbase output whose script is the 0xbb mint template
4. Reject cross-asset value (ORU inputs cannot fund RES outputs, and the reverse)
5. If the transaction destroys ORU on 0xbd and creates RES, require
   R = 0, or R · (2^25 + h)^2 ≤ B << 40
6. Spend a UTXO only through VerifyRigorPredicate
```

Classification is a function of `scriptPubKey` bytes. It does not mint.

### 2.3 Convergence

Convergence is one positive-value `0xbd` output in a body transaction. It is not a draw, and it does not select a winner.

```
R > 0  →  R · (2^25 + h)^2 ≤ B << 40
R = 0  →  the destroy is allowed
```

`R` is the sum of Witness v1 RES atoms created in that transaction. `B` is the ORU atoms in the single `0xbd` output. `h` is the height of the block that confirms the transaction. Consensus does not divide. Display `destroy_cost(h)` is not an input to the predicate.

A block may contain zero or more such transactions. There is no per-block mint count of one.

### 2.4 Integer Arithmetic Domain

| Operation | Rule |
|-----------|------|
| Mint predicate | One multiplication, one square, one shift, compared in unsigned 128-bit arithmetic |
| ORU and RES amounts | `CAmount` int64. Overflow rejects the transaction |
| Display destroy cost | Floor of `P0 · (2^25 + h)^2 / 2^50`. Not fed back into the predicate |

No floating point on the consensus path.

---

## 3. Absence of Global Mutable State

### 3.1 What Does NOT Exist (Consensus)

| Pattern | Status |
|---------|--------|
| Global account balance table | Absent. UTXO only |
| Per-address nonce | Absent |
| Draw seed, winner, or σ input | Absent. Mint does not read a lottery output |
| External oracle | Absent |
| Administrative mint flag | Absent |
| Asset field beside `scriptPubKey` | Absent. Asset class is inferred |

### 3.2 Observational Mirrors (Non-Consensus)

Updated during `ConnectBlock` and not inputs to the mint predicate or the subsidy:

| Mirror | Role |
|--------|------|
| `RuntimeSupplyState` | Export |
| Chain-index supply caches | RPC and recompute on connect |
| `getconvergencestatus` totals | Observation of minted RES and destroyed ORU |

### 3.3 Node-Local Non-Deterministic State

These affect what a miner may include. They do not change the validity of a given block.

| State | Impact |
|-------|--------|
| Mempool order | Which valid transactions a miner selects |
| Peer set | When a block arrives |
| Template refresh | Job freshness |

Two nodes that accept the same block at the same height reach the same UTXO set.

---

## 4. Replayability Conditions

### 4.1 Chain Replay (Full Node)

| Input | Description |
|-------|-------------|
| Block bytes | The chain segment |
| Network parameters | `main`, `test`, `regtest`, `devtest`, or `slowtest` as selected by that build |
| Source revision | The rule set under replay |

Replay is sequential `ConnectBlock` from genesis. The same block sequence yields the same UTXO set.

### 4.2 Mint Replay (Single Transaction)

The inputs that decide a mint are the transaction, the parent UTXO view, and the confirming height:

```
R, B, h
R > 0  →  R · (2^25 + h)^2 ≤ B << 40
```

No candidate set, bind hash, or probability target is an input.

### 4.3 What Replay Does Not Include

Pool share logs, winner previews, and simulator JSONL are operational records. They are not `ConnectBlock` inputs.

---

## 5. Hash-Consistency Assumptions

### 5.1 Hash Functions

| Function | Use |
|----------|-----|
| OpuHash = FIPS SHA3-256(`"RGRPOW01" \|\| header_80`) | Proof-of-work. Suffix `0x06`. Not Ethereum Keccak-256 |
| SHA256d of the 80-byte header | Block identifier |
| SHA256d | Merkle root and txid |
| Ed25519 | Spend authorization of witness v0 ORU and witness v1 RES |

### 5.2 Preimages That Are Not Mint Inputs

A Convergence output is 22 bytes: a 20-byte push of `HASH160` followed by `0xbd`. The mint check reads `B`, `R`, and `h`. It does not hash a winner preimage, and the coinbase does not commit a draw root.

Changing the mint inequality, the subsidy, or the OpuHash preimage would be a different protocol.

### 5.3 Endianness

| Field | Encoding |
|-------|----------|
| Amounts | `CAmount` int64 little-endian on the wire |
| txid | SHA256d internal byte order |

---

## 6. Monetary Invariants

| ID | Rule |
|----|------|
| M-01 | Height 0 pays 0 ORU. Every height ≥ 1 pays 128 ORU. There is no halving |
| M-02 | Mainnet coinbase RES is 0 (`bad-cb-res-amount`) |
| M-03 | RES is created only by a body transaction under the §2.3 inequality |
| M-04 | `RES_MAX_SUPPLY` is the closed-form upper bound, 2^22 RES. Validation does not consult a cumulative counter. One transaction with `R` above that bound fails `CheckResMintArithmetic` |
| M-05 | A `0xbd` output does not enter the UTXO set |
| M-06 | RFC-018 destroys `floor(fee / 2)` of the ORU fee on a RES-transfer. Pure ORU spends and Convergence transactions are outside that sink |
| M-07 | RES does not convert back to ORU |

The ledger scalar `subsidy − Convergence` is not the same quantity as circulating ORU once RFC-018 destruction or an under-claimed coinbase has occurred.

---

## 7. Data Flow Map

### 7.1 Consensus Data Plane

```
Block B_h
  header | coinbase | body transactions
        |
        +-- OpuHash(header) ≤ target
        +-- coinbase ORU ≤ claimable fees + subsidy
        |   coinbase RES = 0 on mainnet
        +-- each body tx:
              classify scriptPubKey
              VerifyRigorPredicate on spends
              0xbd destroy B, optional Witness v1 RES sum R
              mint inequality at height h
        |
        v
UpdateCoins
  0xbd and zero-value 0x6a are not UTXOs
```

### 7.2 Mining Data Plane

SOLO mining copies `getblocktemplate` coinbase bytes, grinds the OpuHash nonce, and calls `submitblock`. Stratum is not a SOLO work source. The coinbase address is an ORU witness v0 address. RES is not a coinbase output.

### 7.3 Observer Data Plane

```
ConnectBlock
    |
    +-- getblock
    +-- getblockconvergence      cumulative destroyed ORU through this block
    +-- getconvergencestatus     destroy cost, cumulative RES, tip
    +-- listconvergencehistory   convergence transactions in a height range
    |
    v
Explorer index (derived)
```

Observer data follows the node. It is not a second ledger. `getconvergenceresult` is not an RPC on this node.

---

## 8. Determinism Boundary Matrix

| Operation | Deterministic for a fixed block? | Consensus authority? |
|-----------|----------------------------------|----------------------|
| `ConnectBlock` | Yes | Yes |
| `CheckResMintArithmetic` | Yes | Yes |
| `GetBlockSubsidy` | Yes | Yes |
| Mempool admission | Yes, given the same mempool policy and view | No. Policy |
| `getblocktemplate` selection | No. Miner choice among valid transactions | No |
| Explorer indexing | Yes, given the same RPC snapshot | No |
| Coq model | Deterministic inside the model | No. `equivalence_claimed = false` |

---

## 9. Known Non-Determinism Boundaries

| Boundary | Description |
|----------|-------------|
| Mempool ordering | Miners may select different valid transaction sets |
| Block timestamp | Inside the consensus time rules |
| Peer timing | Propagation |
| Explorer lag | The index can trail the tip |
| Wallet construction margin | The wallet may price a mint at tip + 144. Consensus uses the confirming height |

---

## 10. Verification Boundary

Formal artifacts can show properties of a model. They do not, by themselves, prove that this C++ node is a refinement of that model.

```
equivalence_claimed = false
refinement_proved = false
```

---

## 11. Replay Checklist

- Block bytes and parent UTXO view are the inputs
- Height used in the mint inequality is the confirming height
- Subsidy is 0 at height 0 and 128 ORU after that
- Coinbase RES on mainnet is 0
- `0xbd` outputs are absent from the replayed UTXO set
- Witness v0 programs are 20 bytes. A 32-byte witness v0 program is not an ORU output
- Observer totals are labeled as derived when they are not the UTXO set
