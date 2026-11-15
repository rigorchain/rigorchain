# Appendix — Protocol Constants and Engineering Reference

Chapters 1–12 are the public argument. This appendix is lookup. It
does not add a claim, rewrite `core/src/`, or write a day’s theorem
count as a protocol parameter.

The reference implementation realizes these parameters in:
`core/src/consensus/params.h`,
`core/src/consensus/rigor_consensus_lock.h`.
Those files are not a separate protocol authority. The one public sentence
lives in Chapter 8 §8.5. This appendix does not repeat it.

---

## A. Frozen public parameters

| Symbol | Value |
|--------|-------|
| ORU subsidy | 0 ORU at height 0; 128 ORU at every height ≥ 1. No halving |
| Mint predicate | `R > 0 → R · (2²⁵ + h)² ≤ B << 40` |
| Display form | `destroy_cost(h) = 1024 · (2²⁵ + h)² / 2⁵⁰` ORU per whole RES (`2⁵⁰` = curve normalization denominator, not a consensus shift) |
| Genesis whole-coin cost | 1024 ORU / 1 RES (`destroy_cost(0) = 102400000000` atoms) |
| `RES_DESTROY_POLE` | `2²⁵ = 33,554,432` |
| `RES_DESTROY_SHIFT` | `40 = 50 − log₂(1024)` (integer consensus scaling factor `B << 40`; not the `destroy_cost` denominator) |
| Derived ceiling | `2⁷ / 2¹⁰ · 2²⁵ = 2²² = 4,194,304` RES (`419430400000000` atoms) |
| Cumulative helper | `floor(2²² · RES_COIN · h / (h + 2²⁵))` atoms for `h > 0`. Not the sum of per-block integer mint maxima |
| `RES_COIN` | 100000000 atoms per whole coin |
| Mint locus | Body tx: unique positive `OP_CONVERGE` + same-tx Witness v1 RES |
| Mint maturity | 100 (same as coinbase; reorg protection) |
| RFC-018 | Half of RES-transfer ORU fees unclaimable: `Σ floor(fee(tx) / 2)` |
| Reverse | RES → ORU forbidden |

Probes (atoms, display helper; display rounding/floor do not enter consensus; consensus uses the inequality):

| Height | `destroy_cost(h)` atoms | ORU / 1 RES |
|-------:|------------------------:|------------:|
| 0 | 102400000000 | 1024 |
| 210000 | 103685749152 | 1036.85749152 |
| 420000 | 104979520049 | 1049.79520049 |

---

## B. Mint reject conditions

Fail-closed. Public form (RFC-020):

| Reject | Meaning |
|--------|---------|
| `bad-txns-res-mint-underpaid` | `R · (2²⁵ + h)² > B << 40` |
| `bad-txns-res-mint-no-burn` | RES output present, no `OP_CONVERGE` output |
| `bad-txns-res-mint-cap-overshoot` | would exceed `419430400000000` atoms |

---

## C. Scripts and identities

| Shape | Identity | Spendable |
|-------|----------|-----------|
| Witness v0, 20-byte program | ORU | yes |
| Witness v1, 32-byte program | RES | yes (if legally born) |
| `<HASH160> 0xbd` (`OP_CONVERGE`) | ORU destroy container | no; not inserted in the UTXO set |

New monetary outputs must not be P2PKH, P2SH, P2WSH, bare multisig,
unknown witness version, or Taproot. Witness v1 is not Taproot.
Spend signatures are Ed25519.

Coinbase states this block’s height and pays ORU only.

---

## D. Proof-of-work

| Item | Value |
|------|-------|
| PoW hash | OpuHash = SHA3-256(`"RGRPOW01" \|\| header_80`) |
| Valid | `OpuHash(header) ≤ target` |
| Block identity | SHA256d (same 80-byte header; not OpuHash) |
| ASERT spacing | 600 s |
| ASERT half-life | 43200 s |
| ASERT clamp | ±12.5% |
| Difficulty | not mint cost; does not enter the predicate |
| Height | block-count index; ASERT does not bind `h` to wall-clock time |

---

## E. Engineering index

| Path | What to look up |
|------|-----------------|
| `core/src/` | Reference implementation. Realizes the protocol; does not define it |
| `core/src/consensus/params.h` | Frozen constants |
| `core/src/consensus/rigor_consensus_lock.h` | Lock |
| `formal_verification/rigor_kernel/` | Coq kernel model |
| `formal_verification/rigor_kernel/release/FinalProofInventory.md` | Classified theorem inventory (snapshot, not a parameter) |
| `formal_verification/rigor_kernel/release/FormalizationFrontier.md` | Formalization frontier |
| `docs/verification/FORMAL-VERIFICATION-STATUS.md` | Production rules versus what a model does not prove |
| `docs/developer/CODE_SSOT_PROTOCOL_FACTS.md` | Code-fact extract |
| `docs/specification/RIGOR_OPCODE_SPECIFICATION.md` | Opcodes |
| `docs/mining/CONVERGENCE_ROOT_CANONICAL_SPEC.md` | Convergence Root encoding |
| `docs/architecture/CONSENSUS_FINGERPRINT_LIBRARY.md` | Fingerprint library (observational, not a rule) |
| `/documentation/formal-verification` | Public boundary page |
| `/zero-trust-verification` | Browser recomputation of selected fields |

Noun list (not Chapter 9 narrative): [`09-glossary.md`](./09-glossary.md).

---

## F. Live and historical documents

| Document | Role |
|----------|------|
| RFC-020 | Live RES issuance: continuous `destroy_cost(h)`, flat 128 ORU, body destroy-to-mint |
| RFC-018 | RES-transfer ORU fee-share destroy |
| RFC-000 | Consensus constitution |
| SPEC-002 | ORU / RES recurrences |
| `MAINNET_PROTOCOL_FREEZE_DECLARATION.md` | Freeze declaration |
| RFC-001 / 002 / 008 | Historical specs; code and RFC-020 govern |

This paper does not edit RFC texts. Historical entries are not live
rules.

---

## G. Terms

| Term | Official meaning |
|------|------------------|
| ORU | Operational Resource Unit. The operational asset produced by Proof-of-Work. Medium-of-exchange asset. Subsidy 128 after genesis |
| RES | Reserve End State. The reserve-oriented asset created through irreversible ORU destruction. Designed store-of-value asset within the protocol. Derived ceiling `2²²` RES; not mined |
| Convergence | Same-tx `OP_CONVERGE` destroy of ORU; MAY mint Witness v1 RES under the predicate |
| `destroy_cost(h)` | Display mint cost. `2⁵⁰` is the curve normalization denominator. Consensus computes `B << 40`, not this quotient. Display rounding/floor do not enter consensus |
| OpuHash | Live proof-of-work validity hash (not block identity) |
| Consensus fingerprint | Observational identity over public fields. Does not accept or reject blocks |

Lookup ends here. The claims are in Chapters 1–12.

Algebraic derivation of the mint identity is Appendix D:
[`../RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md`](../RIGOR_CONVERGENCE_MATHEMATICAL_NOTE.md).
That Mathematical Design Note is explanatory documentation. It does
not introduce new consensus rules.
