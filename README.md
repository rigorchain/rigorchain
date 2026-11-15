# RIGOR

A Proof-of-Work UTXO Architecture for Two Native Monetary Roles

Display version is `RIGOR_RELEASE_VERSION` in `core/src/clientversion.h`.

RIGOR is a Bitcoin-inspired UTXO blockchain protocol exploring a
different approach to native monetary design.

The protocol separates two monetary roles at the consensus layer:

- **ORU** — the transaction and security asset
- **RES** — a separately defined scarce asset created through
  deterministic Convergence

Both are native ledger objects verified by the same full-node consensus
rules.

RIGOR does not introduce an account model, smart-contract execution
layer, or external asset registry.

Its design focuses on a narrower question:

> Can different monetary functions be separated while preserving the
> verification properties of a Bitcoin-style UTXO system?

---

## Design Principles

RIGOR is built around several constraints:

- Single UTXO state space
- Script-defined asset identity
- Proof-of-Work chain ordering
- Deterministic monetary transformation
- Minimal consensus surface

Asset identity is determined by consensus-defined `scriptPubKey`
classification.

It is not maintained by a registry, explorer interpretation, or wallet
metadata.

The node decides validity.

Products display public data.

---

## Verification Boundary

RIGOR distinguishes:

```
Protocol model
≠
Running C++ implementation
≠
Product interfaces
```

Formal verification covers properties of the protocol model.

It does not claim C++ equivalence, compiler correctness, network
guarantees, or economic outcomes.

```
equivalence_claimed = false
cpp_formal_equivalence = false
documentation_consensus_authority = false
```

---

## Protocol Verification Status

Protocol specifications are normative SSOT.
Implementations realize the frozen protocol.

Philosophy:

- [RIGOR Principles](docs/philosophy/RIGOR_PRINCIPLES.md)
- [Protocol Boundary](docs/philosophy/RIGOR_PROTOCOL_BOUNDARY.md)

Protocol:

- [Consensus Invariants](docs/protocol/RIGOR_CONSENSUS_INVARIANTS.md)

Security:

- [Security Patch Boundary](docs/security/RIGOR_SECURITY_PATCH_BOUNDARY.md)

After mainnet activation, consensus invariants fix the protocol
boundary: monetary issuance, asset separation, script templates,
transaction authorization, proof-of-work identity, and ledger
transition.

Changing these rules is not a software upgrade. It defines a different
protocol and a separate network.

---

## Core Monetary Parameters

RIGOR monetary behavior is defined by a small set of immutable consensus constants.

| Parameter | Value | Meaning |
|---|---|---|
| ORU block subsidy | 2^7 | 0 ORU at height 0; 128 ORU at every height ≥ 1 |
| Initial RES destruction cost | 2^10 | 1024 ORU per RES at initial scale |
| Convergence scale parameter | 2^25 | Controls RES cost growth |
| Theoretical maximum RES supply | 2^22 | 4,194,304 RES |

These parameters are consensus constants.

They are not governance parameters.

Changing these values would define a different protocol.

Full specification:

[RIGOR Core Parameters Specification](https://github.com/rigorchain/rigorchain/blob/main/docs/protocol/RIGOR_CORE_PARAMETERS_SPECIFICATION.md)

---

## Treasury & Market Neutrality

RIGOR is designed to be a protocol and network, not a price-support operation.

From Mainnet Genesis, the RIGOR project adopts a public Treasury & Market Neutrality Policy governing the use of project-controlled resources and the project's conduct toward the ORU and RES markets.

The project may actively:

* develop and maintain the protocol;
* operate public infrastructure;
* improve network security;
* support independent miners and developers;
* fund security audits and research;
* develop wallets, mining software and tooling;
* support legitimate ecosystem and exchange integrations.

The project does **not** make maintaining or increasing the market price of ORU or RES an operational objective.

The project will not knowingly coordinate:

* artificial market demand;
* pump-and-dump activity;
* wash trading;
* artificial trading volume;
* price support or price floors;
* misleading market signals;
* preferential non-public information for trading purposes.

Independent participants remain free to buy, hold, sell, mine, develop, provide liquidity for, or otherwise participate in the RIGOR ecosystem.

The market determines the economic value of RIGOR.

The project's responsibility is to build a network worthy of independent participation.

> **Build the protocol. Build the infrastructure. Do not control the market.**

**Full policy:** [`TREASURY_MARKET_NEUTRALITY_POLICY.md`](./TREASURY_MARKET_NEUTRALITY_POLICY.md)

---

## Technical Reference

Architecture:
[`docs/architecture/RIGOR_ARCHITECTURE_PAPER.md`](docs/architecture/RIGOR_ARCHITECTURE_PAPER.md)

Core monetary parameters:
[`docs/protocol/RIGOR_CORE_PARAMETERS_SPECIFICATION.md`](https://github.com/rigorchain/rigorchain/blob/main/docs/protocol/RIGOR_CORE_PARAMETERS_SPECIFICATION.md)

Protocol specification:
[`docs/whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md`](docs/whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md)

Consensus rules:
[`docs/protocol/RFC-020.md`](docs/protocol/RFC-020.md)

Formal verification boundary:
[`docs/whitepaper/RIGOR_FORMAL_VERIFICATION_DESIGN.md`](docs/whitepaper/RIGOR_FORMAL_VERIFICATION_DESIGN.md)

Implementation:
[`core/`](core/)

---

## 1. What is RIGOR

RIGOR is a dual-asset UTXO protocol. ORU is the utility asset. RES may be
minted only by destroying ORU at `destroy_cost(h)` when
consensus-defined issuance conditions hold. Asset class is inferred from
`scriptPubKey`. There is no account model and no administrative key on
the consensus path.

**Whitepaper:** [`docs/whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md`](docs/whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md)

Short companions (not a substitute for the paper):

| Paper | Question |
|-------|----------|
| [`docs/whitepaper/RIGOR_PROTOCOL_IDENTITY.md`](docs/whitepaper/RIGOR_PROTOCOL_IDENTITY.md) | What is RIGOR? |
| [`docs/whitepaper/RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md`](docs/whitepaper/RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md) | How does the mathematics run? |
| [`docs/whitepaper/RIGOR_FORMAL_VERIFICATION_DESIGN.md`](docs/whitepaper/RIGOR_FORMAL_VERIFICATION_DESIGN.md) | What is the model, and where does proof stop? |

Protocol SSOT: [`docs/protocol/RFC-020.md`](docs/protocol/RFC-020.md),
[`docs/protocol/RFC-018.md`](docs/protocol/RFC-018.md).

---

## 2. Protocol identity

| Field | Production value |
|-------|------------------|
| Chain name | `RIGOR` |
| Mainnet genesis | `e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468` |
| Magic bytes | `ce f1 db fa` |
| P2P port | `8760` |
| RPC port | `8761` |
| Bech32 HRP | `rg` |
| Consensus lock | `RIGOR_CONSENSUS_LOCK_VERSION = 3` |

Lock files: `core/src/consensus/params.h`,
`core/src/consensus/rigor_consensus_lock.h`.

AlphaPlus / Alpha1 names are historical release channels, not the current
protocol identity. See [`CHANGELOG.md`](CHANGELOG.md).

Extracted facts:
[`docs/developer/CODE_SSOT_PROTOCOL_FACTS.md`](docs/developer/CODE_SSOT_PROTOCOL_FACTS.md).

---

## 3. Consensus overview

```
RIGOR =
    Dual Native Assets
  + UTXO Monetary Model
  + Deterministic Convergence
  + RES-transfer ORU fee destruction
  + Adaptive Proof-of-Work (ASERT)
  + Cryptographic Ownership (Ed25519)
```

Consensus state is the UTXO set plus validation rules. Rebuildable indexes
are not monetary truth.

- Ordinary transfers cannot mint ORU or RES.
- ORU enters through the coinbase subsidy: **0** at height 0 and **128 ORU**
  at every height ≥ 1. That subsidy is the mining-related issuance path.
- RES is not a coinbase reward. On mainnet the coinbase RES amount must be
  **0**. A nonzero coinbase RES amount is rejected with `bad-cb-res-amount`.
- RES is created only in a body transaction. That transaction destroys ORU
  in one `RIGOR_MARKER_CONVERGE` (`0xbd`) output of `B` ORU atoms and may
  create Witness v1 RES outputs summing to `R` RES atoms. At confirming
  height `h`, `R > 0` is valid only when `R · (2²⁵ + h)² ≤ B << 40`.
  `R = 0` is a plain destroy. The rule is deterministic. There is no draw
  and no winner.
- `ConnectBlock` is the admission path. Explorer display is observation.

There is **no Bitcoin Script VM**. Spends are structured predicates
(`VerifyRigorPredicate`). Accepted markers are RETURN `0x6a` (NULL_DATA,
value 0) and CONVERGE `0xbd`. `0xbb` is not a valid RES mint marker.
Recognizing a byte in the classifier does not make a transaction that uses
it valid: a body transaction carrying `0xbb` is rejected with
`bad-txns-res-mint-noncoinbase`.

Mathematics: [`docs/whitepaper/RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md`](docs/whitepaper/RIGOR_MATHEMATICAL_CONSENSUS_DESIGN.md).  
Opcode table: [`docs/specification/RIGOR_OPCODE_SPECIFICATION.md`](docs/specification/RIGOR_OPCODE_SPECIFICATION.md).

---

## 4. Native assets

| Asset | Role |
|-------|------|
| **ORU** | Transaction asset, fee asset, mining subsidy, Convergence burn |
| **RES** | Reserve asset. Issued only when consensus-defined issuance conditions hold after Convergence. **Not a PoW reward.** |

PoW does not create RES.

ORU: 8 decimals (`COIN = 1e8`). Genesis subsidy is **0**. Height ≥ 1
pays **128 ORU** forever. No ORU cap.

RES: 8 decimals (`RES_COIN = 1e8`). Cap `RES_MAX_SUPPLY =
419430400000000` atoms (`2²²` RES). Minted only in a body transaction
that burns ORU (`OP_CONVERGE`) and creates Witness v1 RES in the same
transaction when `R · (2²⁵+h)² ≤ B << 40`. `2⁴⁰` is the integer
consensus scaling factor. Display form
`destroy_cost(h) = 1024 · (2²⁵+h)² / 2⁵⁰` uses `2⁵⁰` as the curve
normalization denominator; that denominator is not a consensus shift.
Display rounding/floor do not enter consensus.

RFC-018: half of the ORU fee on a RES-transfer is unclaimable. That burn
is not a UTXO and not a wallet balance.

---

## 5. Mining

**Node consensus PoW** is OpuHash: FIPS 202 SHA3-256 over a domain-tagged
header (domain suffix `0x06`):

```
digest = SHA3-256("RGRPOW01" || header_80)
```

OpuHash is not Ethereum Keccak-256 (suffix `0x01`). The block identifier
is SHA256d of the same 80-byte header, not the OpuHash digest.
`CheckProofOfWork` calls `GetRigorPowHash`. It does not call `GetOpusHash`.

Difficulty is ASERT (aserti3-2d), mainnet half-life **12 hours** (43200 s),
anchor height 1, ±12.5% per-block clamp. Target spacing 10 minutes.

SOLO path: `getblocktemplate` → copy canonical coinbase bytes → nonce
search → `submitblock`. The node builds ORU subsidy and scriptSig.
RES mint is a body transaction, not a coinbase prize. Stratum is not a
SOLO URL.

The repository includes a reference miner implementation (`minerd`) that
searches SHA3-256 `"RGRPOW01" || header_80`, matching node
`CheckProofOfWork`.

SOLO mining uses a miner-owned local RIGOR node and does not require a
publicly reachable mining RPC endpoint.
[`docs/mining/SOLO_MINING.md`](docs/mining/SOLO_MINING.md).

What the project provides for mining, and what stays open to independent
miners and operators: [`docs/mining/MINING_SCOPE.md`](docs/mining/MINING_SCOPE.md).

[`miner/README_MINER.md`](miner/README_MINER.md).

---

## 6. Wallet

`rigor-qt` / `rigor-wallet` / `rigor-cli`. Dual-asset UTXO wallet
supporting ORU and RES outputs. Backup/restore with `backupwallet` /
`restorewallet`.

The wallet constructs transactions. Consensus decides Convergence
outcomes and RES mints. The wallet does not mine.

Guide: [`docs/user/Wallet.md`](docs/user/Wallet.md).

---

## 7. Explorer

Independent observation product. Not consensus. Not a ledger of record.

Display identity matches the node: genesis
`e2ce92cf…1468`, HRP `rg`, RPC `8761`.

[`docs/explorer/EXPLORER_ARCHITECTURE.md`](docs/explorer/EXPLORER_ARCHITECTURE.md).

---

## 8. Security model

| Layer | Role |
|-------|------|
| UTXO conservation | Ordinary transfers cannot create ORU or RES |
| Script validation | Restricted monetary script families; classification ≠ issuance |
| Proof-of-Work | OpuHash = SHA3-256 (`RGRPOW01`) with ASERT difficulty |
| Ed25519 | Cryptographic ownership of spendable outputs |
| Formal protocol properties | Model invariants — see section 9 |

Vulnerability reporting: [`SECURITY.md`](SECURITY.md). Do not expose
JSON-RPC to the public Internet.

Security boundary: [`docs/security/RIGOR_SECURITY_PATCH_BOUNDARY.md`](docs/security/RIGOR_SECURITY_PATCH_BOUNDARY.md).

---

## 9. Verification model

```
equivalence_claimed = false
hash_kernel_shared_binary_with_cpp = false
refinement_proved = false
```

Formal verification proves properties of the protocol model. It does not
prove that the C++ implementation is a refinement of that model.

Operator map: [`docs/formal/VerificationStatus.md`](docs/formal/VerificationStatus.md).

---

## 10. Build instructions

See [`docs/developer/Build.md`](docs/developer/Build.md) and `core/doc/build-*.md`.

```bash
cd core
./autogen.sh
./configure
make -j$(nproc)
```

Binaries: `rigord`, `rigor-cli`, `rigor-qt` (if Qt enabled), `rigor-wallet`.

Miner: `cd miner && ./autogen.sh && ./configure && make`.  
Explorer: `explorer/` (Node).

---

## 11. Release information

Code signing policy: [`docs/CODE_SIGNING_POLICY.md`](docs/CODE_SIGNING_POLICY.md).

Free code signing provided by SignPath.io, certificate by SignPath Foundation

That sentence is the SignPath Foundation wording. It does not mean a certificate has been issued. Roles: [`docs/RELEASE_AND_SIGNING_ROLES.md`](docs/RELEASE_AND_SIGNING_ROLES.md). Privacy: [`docs/PRIVACY.md`](docs/PRIVACY.md).

This repository may contain development and internal validation artifacts.
Public release status is determined by tagged releases.

Current binaries: [GitHub Releases](https://github.com/rigorchain/rigorchain/releases).
Version history: [`CHANGELOG.md`](CHANGELOG.md).
A past tag’s notes (example): [`docs/release/RIGOR_V0.1.3_RELEASE_NOTES.md`](docs/release/RIGOR_V0.1.3_RELEASE_NOTES.md).

---

## Repository layout

| Path | Purpose |
|------|---------|
| `core/` | Full node, wallet, consensus |
| `docs/` | Active documentation (`docs/README.md`) |
| `explorer/`, `miner/`, `web/` | Product components |
| `verification/` | Public verification map |
| `formal_verification/` | Coq model, golden vectors, manifests |

Documentation classification: [`docs/README.md`](docs/README.md).

## References

- Whitepaper: [`docs/whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md`](docs/whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md)
- Protocol RFCs: [`docs/protocol/RFC-020.md`](docs/protocol/RFC-020.md), [`docs/protocol/RFC-018.md`](docs/protocol/RFC-018.md)
- Design references: see the whitepaper’s *References and Design Context*

## License

MIT — see [LICENSE](LICENSE).
