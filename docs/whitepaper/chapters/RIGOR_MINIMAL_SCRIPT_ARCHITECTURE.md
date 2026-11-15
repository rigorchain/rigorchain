# RIGOR Minimal Script Architecture

**Document status:** Production specification

This chapter explains a core design principle of RIGOR:

**RIGOR inherits the battle-tested Bitcoin UTXO architecture while reducing execution complexity, limiting script capability, enforcing strict asset isolation, and minimizing consensus attack surface.**

It does not change consensus rules, wallet logic, mining logic, or production code. Public mathematics: [`../RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md`](../RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md). Engineering specs: [`../../specification/`](../../specification/). RFC-019 and earlier texts are not protocol authority.

---

## 1. Introduction: Why Minimal Script Architecture

RIGOR does not attempt to compete with programmable blockchain platforms.

General-purpose chains often increase functionality by introducing virtual machines, arbitrary smart contracts, large scripting systems, and dynamic global state. Those features enlarge what a chain can express. They also enlarge what a chain must defend.

RIGOR focuses on:

- monetary integrity
- deterministic consensus
- verifiable asset ownership
- minimized attack surface

A blockchain securing monetary value does not necessarily require unlimited programmability. More code paths create more possible failures. A smaller execution surface creates stronger verification capability.

The goal is not maximum programmability.

The goal is maximum verifiability.

**Security through minimalism** is the operating principle. Script exists so ownership and asset identity can be checked. Script does not exist so the ledger can become a general-purpose computer.

RIGOR minimizes script complexity rather than eliminating scripts. The protocol intentionally restricts execution semantics to preserve deterministic verification and reduce consensus attack surface.

RIGOR is a Bitcoin-derived UTXO architecture, a native dual-asset system, a restricted script environment, and a deterministic validation system.

RIGOR is not a general-purpose programmable blockchain, a smart contract platform, an EVM-compatible chain, a Taproot extension, a token layer, a colored coin system, or an inscription/Rune system.

Official type list: `docs/security/RIGOR_SCRIPT_TYPE_REGISTRY.md`.  
Security boundary: `docs/security/RIGOR_SCRIPT_SECURITY_BOUNDARY.md`.

```
Less capability
       ↓
Less complexity
       ↓
Smaller attack surface
       ↓
Stronger mathematical verification
```

---

## 2. Bitcoin UTXO Foundation

RIGOR is based on the Bitcoin UTXO model.

The node heritage is Bitcoin Core v25. Storage of an output remains:

```
TxOut = (nValue, scriptPubKey)
```

There is no account model as consensus truth. There is no parallel asset field on `CTxOut`. Ownership changes only by spending previous outputs and creating new ones.

### Explicit ownership

Each asset exists as an unspent transaction output.

```
Previous UTXO
      |
      |
Transaction Validation
      |
      |
New UTXO Owner
```

There are no hidden balances. There is no global mutable account state. A full node does not consult an external registry to know what an output is or who may spend it.

### Deterministic validation

Every node independently verifies:

- transaction inputs
- outputs
- signatures
- asset rules
- consensus conditions

The same input must produce the same validation result.

```
same accepted block bytes  →  same validity decision
```

### Proven security model

Bitcoin’s UTXO architecture has operated at global scale for many years. RIGOR preserves this proven foundation while reducing unnecessary complexity: witness versions are restricted, Taproot/BIP341 execution is not inherited, spend signatures are Ed25519-only, and a general-purpose VM is not implemented.

This is not a claim that RIGOR is Bitcoin. It is a claim that the accounting model — explicit UTXOs, stateless verification, independent replay — is the part worth keeping.

---

## 3. Script-Is-Asset Architecture

### Script Defines Asset Identity

In RIGOR, asset identity is not stored as external metadata.

The script structure itself defines the asset domain.

```
Script Structure
        |
        |
Asset Classification
        |
        |
Validation Rules
```

`scriptPubKey` is parsed as a witness program. Witness version and program shape classify the output as ORU, RES, or (for zero-value `OP_RETURN`) none. Classification is consensus-visible. A full node does not require an indexer, inscription parser, or off-chain color database to know which asset pool an output belongs to.

Script is the **compliance/classification layer**: it states the conditions under which an output may exist and later be spent. Asset **class** is inferred from `scriptPubKey`. Asset ownership and transfer are enforced through UTXO consumption, script validation, signature verification, and consensus predicates. Script alone does not authorize asset creation. RES creation requires Deterministic Reserve Issuance (`destroy_cost(h)`) and a same-tx `OP_CONVERGE` destroy. Parsing a script is not a substitute for those proofs.

This avoids:

- hidden asset metadata
- external indexing dependency
- inconsistent interpretation

The earlier shorthand “the script is the asset” correctly forbade a parallel asset field. The precise rule is: **script defines identity; consensus defines legitimacy.**

---

## 4. Dual Native Asset Architecture

ORU and RES are **not**:

- colored coins
- Bitcoin inscriptions
- Ordinals
- Runes
- smart contract tokens
- wrapped assets
- external accounting layers

They are **native UTXO assets**.

Both assets are first-class protocol-native assets.

> ORU and RES are two native asset identities inside the RIGOR UTXO system. Neither asset is a secondary representation of another asset.

Both exist at the same protocol layer. There is no parent-child asset relationship.

### ORU — Medium of Exchange

**Role:** Medium of Exchange

**Characteristics:**

- transactional asset
- mining reward
- network utility
- liquidity layer

**Script path (spendable):** Witness v0 (`P2WPKH` 20-byte program, or `P2WSH` 32-byte program).

**Convergence destroy:** `OP_CONVERGE` / `RIGOR_MARKER_CONVERGE` (`0xbd`) —
ORU-domain, unspendable, not a RES output. The same transaction MAY mint
Witness v1 RES at `destroy_cost(h)`.

ORU pays fees, miner subsidy, and Convergence entry weight. Coinbase creates ORU. Convergence consumes ORU.

### RES — Store of Value

**Role:** Store of Value

**Characteristics:**

- scarcity asset
- derived cap (**419430400000000** atoms = **4194304 RES**)
- same-tx body mint at `destroy_cost(h)`
- long-term holding asset

**Script path:** Witness v1, 32-byte program, **not** CVG2.

RES is minted in the **block body** of the same transaction that destroys
ORU (`OP_CONVERGE`) when `R > 0 → R · (2²⁵ + h)² ≤ B << 40`.
Proof-of-work does not create RES. Coinbase `OP_RES_MINT` is not the
RFC-019 path. `No Convergence → No RES`.

| | **ORU** | **RES** |
|--|---------|---------|
| Protocol layer | Native UTXO | Native UTXO |
| Spendable classification | Witness v0 | Witness v1 (not CVG2) |
| Other native object | CVG2: Witness v1, ORU-domain, unspendable | — |
| Role | Medium of exchange | Store of value |
| Origin | Coinbase subsidy | Same-tx body mint at `destroy_cost(h)` |
| Relationship | Peer asset | Peer asset |

Conservation is enforced **per asset pool**. An ORU input cannot fund a RES output. A RES input cannot fund an ORU output. Isolation is a consensus rule, not a wallet convention.

---

## 5. Script Restrictions and Attack Surface Reduction

RIGOR intentionally limits script functionality.

RIGOR does **not** provide:

- a general-purpose VM
- arbitrary smart contracts
- a programmable execution layer
- Turing-complete execution
- unrestricted programmable state

RIGOR **does** provide:

- constrained transaction validation
- asset-specific script rules
- deterministic verification logic

RIGOR minimizes script complexity rather than eliminating scripts. The protocol intentionally restricts execution semantics to preserve deterministic verification and reduce consensus attack surface.

Allowed chain outputs are a closed set:

| Allowed output | Role |
|----------------|------|
| Witness v0, 20-byte program | ORU. Spend: witness `<sig64><pk32>`, program = HASH160(pk) |
| Witness v1, 32-byte program, not `CVG2` or `RES2` | RES. The program is the Ed25519 public key |
| 22-byte `HASH160 \|\| 0xbd` | ORU destroy. Not a UTXO |
| Zero-value `0x6a` | NULL_DATA |

A 32-byte witness v0 program (P2WSH) is `NONSTANDARD` and is not an ORU output. P2PKH, P2SH, bare multisig, Taproot, and secp256k1 CHECKSIG are not consensus templates. There is no Script VM.

Spend signatures are **Ed25519 only**. secp256k1 / ECDSA CHECKSIG hard-fails (`SCRIPT_ERR_SECP256K1_DISABLED`).

Security benefits — reduced:

- execution complexity
- consensus bugs
- unexpected state transitions
- verification burden

> Every additional execution capability creates additional consensus responsibility.

RIGOR accepts less expressiveness so that every honest node can carry that responsibility.

---

## 6. RIGOR Script Model

Production RIGOR classifies output scripts and authorizes spends with `VerifyRigorPredicate`. It does not execute a Bitcoin Script program. `OP_DUP`, `OP_HASH160`, `OP_EQUALVERIFY`, and `OP_CHECKSIG` are not RIGOR instructions.

Accepted shapes:

| Surface | What consensus checks |
|---------|------------------------|
| Witness v0, 20-byte program | ORU. Witness is `<sig64><pk32>`. The program is HASH160 of the 32-byte Ed25519 key. Signing domain `RGRORU01` |
| Witness v1, 32-byte program | RES, when the program does not begin with `CVG2` or `RES2`. The program is the public key. Witness is `<sig64><pk32>` with `pk` equal to the program. Signing domain `RGRRES01` |
| 22-byte converge script | ORU destroy (`0xbd`). Not inserted into the UTXO set. Not spendable |
| Zero-value `0x6a` | Data. Asset NONE |

A 32-byte witness v0 program is P2WSH. `ClassifyRigorTxout` does not accept it. The output is `NONSTANDARD`. No inner witness script runs, and timelock opcodes are not executed.

`0xbb` is not a creatable RES mint template. A body transaction that carries it is rejected with `bad-txns-res-mint-noncoinbase`.

Ed25519 is the spend signature. secp256k1 CHECKSIG is not an authorization path.

### Witness v0

The only witness v0 consensus output is the 20-byte ORU program.

### Witness v1

Witness v1 is the RES key path. It is not a Convergence container.

| 32-byte v1 program | Classification | Spend |
|--------------------|----------------|-------|
| Prefix `CVG2` | INVALID (not creatable) | Unspendable; **not** inserted into the UTXO set |
| Prefix `RES2` (wire spelling) | INVALID (not creatable) | Spend branch deleted; creation rejected |
| Neither prefix (native) | RES | Two-item Ed25519 key-path; program **is the 32-byte pubkey**; witness is `<sig><32-byte pk>` with `pk == program` |

A 36-byte “claim program” size exists in headers for historical wallet/semantic code. `IsAllowedScript` requires a 32-byte v1 program. Those 36-byte programs are **not** consensus chain outputs.

### Witness v1 is not Bitcoin Taproot

**RIGOR does not inherit BIP341 Taproot execution semantics.**

Witness v1 on Bitcoin is Taproot: output key, annex, merkle script paths, tapscript, control blocks. RIGOR removed that path from consensus validation. Comments in `validation.cpp` and `predicate_verify.cpp` record the deletion: no control blocks, no annex, no script-path spends. There is no `interpreter.cpp`.

Witness v1 here is a BIP141-shaped version byte used as the RES public key. Sharing a version number with Bitcoin Taproot is not sharing Taproot’s VM. Convergence is the 22-byte `0xbd` script, not a witness v1 program.

| Bitcoin Taproot (BIP341) | RIGOR Witness v1 |
|--------------------------|------------------|
| Output key + optional script tree | 32-byte program as asset / Convergence identity |
| Control-block script path | **Removed** |
| Annex | **Removed** |
| Tapscript | **Not implemented** |
| secp256k1 key path | **Disabled**; Ed25519 only |

A second historical v1 spend path (an 11-item “PCORB script path”) was removed because authorization did not bind the spending transaction. RES transfers use the Ed25519 key path only.

---

## 7. Convergence Script Architecture

Convergence is not a smart contract.

It is a deterministic consensus mechanism: destroy ORU in `OP_CONVERGE`;
the same body transaction MAY mint Witness v1 RES at `destroy_cost(h)`.

```
ORU destroyed (OP_CONVERGE, amount B)
        ↓
same body transaction
        ↓
Witness v1 RES (sum R) if R > 0 → R · (2²⁵ + h)² ≤ B << 40
```

A user destroys ORU into an **`OP_CONVERGE`** / `RIGOR_MARKER_CONVERGE`
(`0xbd`) output. That output is protocol-controlled:

- classified as ORU-domain (not RES)
- unspendable
- excluded from the UTXO set after in-block validation

Honest nodes check `CheckResMintArithmetic`: `R · (2²⁵ + h)² ≤ B << 40`
at the confirming height. The derived cap is `419430400000000` atoms.
Mint-output maturity 100 is reorg protection.

Convergence outputs are protocol-controlled objects.

They do not introduce arbitrary execution.

There is no Convergence VM, no user-deployed Convergence bytecode, and
no discretionary mint. The script bytes are a destroy container. Live
issuance is RFC-020 integer cost `destroy_cost(h)`.

```
ORU destroyed (OP_CONVERGE, unspendable)
        ↓
same body transaction
        ↓
maybe a RES mint at destroy_cost(h) — never a 1:1 swap
```

---

## 8. Script Security Philosophy

### Why RIGOR Rejects Complex Script Systems

Complex scripting creates:

- larger audit scope
- more edge cases
- more formal verification difficulty

Hidden execution paths make consensus ambiguity cheap to introduce and expensive to prove absent. Implementation divergence becomes likelier as interpreters grow. A monetary protocol that must be independently replayed by strangers cannot afford an unbounded state machine as a dependency of “who owns what.”

RIGOR approach:

```
Less capability

       ↓

Less complexity

       ↓

Smaller attack surface

       ↓

Stronger mathematical verification
```

This is a security architecture, not a feature gap. Programmability is deferred so that monetary rules stay small enough to audit, model, and recompute.

---

## 9. Formal Verification Relationship

Formal verification becomes more realistic when:

- state transitions are limited
- execution paths are restricted
- rules are deterministic

A general-purpose VM makes “prove the chain” a moving target: every new contract class is a new theorem obligation. A closed script family makes the obligation finite. Dual-asset conservation, `OP_CONVERGE` destroys, and fail-closed RES mint at `destroy_cost(h)` are predicates over UTXOs and amounts — not over arbitrary memory.

RIGOR's minimal script design is not a limitation.

It is a prerequisite for stronger mathematical assurance.

The Coq development (`formal_verification/coq`) proves properties of **the model**. Golden-vector and differential gates check finite correspondence with production helpers. They do not collapse into a claim that the running binary is a refinement of the model.

Minimal scripts make that honesty cheaper to maintain: fewer paths to specify, fewer paths to model, fewer paths to diverge.

Public surfaces:

- Whitepaper: this chapter in `docs/whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md`
- Website: `/documentation/security/minimal-script-architecture`
- GitHub: `docs/security/minimal-script-architecture.md`
- Consistency audit: `docs/security/RIGOR_MINIMAL_SCRIPT_CONSISTENCY_AUDIT.md`
- Script type registry: `docs/security/RIGOR_SCRIPT_TYPE_REGISTRY.md`
- Script security boundary: `docs/security/RIGOR_SCRIPT_SECURITY_BOUNDARY.md`
- Formal verification: `/documentation/formal-verification`
- Zero Trust: `/zero-trust-verification`

---

## Sources (read-only audit)

| Source | What it confirms |
|--------|------------------|
| `core/src/consensus/tx_check.cpp` | `IsAllowedScript`: v0/20, native v1/32 (not CVG2/RES2), `OP_RES_MINT`, `OP_CONVERGE`; `OP_RETURN` at nValue 0 |
| `core/src/script/rigor_classify.cpp` | `AssetTypeFromScriptPubKey`: v0/20 → ORU; native v1/32 → RES; `OP_CONVERGE` → ORU; `OP_RES_MINT` → RES; unknown → `ASSET_INVALID` |
| `core/src/script/standard.cpp` | `IsConvergenceScript` / `IsResScript` (thin wrappers over the classifier) |
| `core/src/script/predicate_verify.cpp` | v0 P2WPKH Ed25519; v1 RES `pk == program` then Ed25519; no BIP341; no Script VM |
| `core/src/validation.cpp` | Taproot/BIP341 validation path removed; v1 is Ed25519 key-path |
| `core/src/coins.cpp` | CVG2 outputs are not a consensus type (rejected at CheckTransaction) |
| `docs/developer/Architecture.md` | Script is the compliance layer |
| RFC-015 Alpha1 §5–6 | Dual-asset witness binding |

No file in `core/src/kernel/`, `core/src/consensus/`, `pow.cpp`, `validation.cpp`, the script interpreter, wallet, or miner was modified for this chapter.
