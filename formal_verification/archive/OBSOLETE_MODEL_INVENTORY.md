# Obsolete / residual formal-model inventory

Snapshot date: 2026-08-26  
Task: ``

Historical proofs are **not deleted**. They remain in `formal_verification/coq/`.
This file classifies them against the live RIGOR v0.1 unique path:

```
UTXO → scriptPubKey → AssetTypeFromScriptPubKey → fixed OwnershipPredicate → Ed25519
```

Status labels:

| Label | Meaning |
|-------|---------|
| **OBSOLETE** | Describes a consensus surface that C++ no longer implements. Do not cite as v0.1 spend semantics. |
| **LIVE_OTHER** | Still the right model for a *different* cone (draw, SHA-256, schedule, dual-pool). Not a spend/ownership proof. |
| **PARTIAL** | Shape-aligned with v0.1 classifier, but missing Ed25519 / sighash / chain_id, or still carries retired constructors. |
| **MISSING** | Required by v0.1; not present in the old cone. Supplied by `formal_verification/rigor_v01/`. |

## OBSOLETE (must not be cited as v0.1 spend)

| Model | Where it lived | Why obsolete |
|-------|----------------|--------------|
| EvalScript VM | C++ `interpreter.cpp` (deleted); Coq never had a real VM, but `OpaqueBoundary.v` still names EvalScript as a non-claim | No opcode interpreter in consensus |
| Opcode execution semantics | `script.h` opcode table residual; Coq `CvgCovenant.v` OP_CVG_CLAIM | No opcode authorization path |
| Miniscript semantics | never a Coq module; C++ miniscript tests leftover | Not a consensus language |
| P2WSH semantics | `ScriptMintWitness.canonical_p2wsh` as a *reject* fixture; Python `canonical_p2wsh_script` | v0/32 is `ASSET_INVALID`; no redeem script |
| CHECKMULTISIG semantics | none in Coq; Bitcoin opcode enum leftover in C++ headers | No multisig spend |
| Taproot / BIP341 semantics | comments in interpreter; `XOnlyPubKey` names | RES v1 is Option B key-path, not taproot |
| ECDSA / secp256k1 spend | none in Coq; C++ spend is Ed25519 only | Unique algorithm is Ed25519 |
| Option A `program = SHA256(pk)` | P0-018/P0-019 audit text; old freeze sentence | Live: `program = pk` (`ed25519::ResWitnessProgram` is memcpy) |
| `SigVersion::BASE` sighash alias | P0-018 finding H-1 (pre-remediation) | Live: three tags `RGRORU01` / `RGRRES01` / `RGRMNT01` |
| Trailing `uint32_t{1}` only domain tag | P0-018 pre-remediation `SignatureHash` | Replaced by prefix tags + `chain_id` |
| PCORB *script path* on witness v1 RES | `ScriptMintWitness.parse_pcorb_shape`; `ScriptShape.pcorb_shape_ok` | C++ deleted the 11-item path (SECURITY-001-A / A-7) |
| OP_CVG_CLAIM / AssetCVG / covenant mint | `ReplayReorgCore/CvgCovenant.v` (snapshot: `archive/coq/CvgCovenant.v`) | C++ has no opcode; classifier maps claim bytes to INVALID |
| Legacy account-model ConnectBlock | `coq/legacy_account_model/` | Global scalars, not UTXO; README already says not EvalScript / ConnectBlock |

## PARTIAL (keep, do not treat as the unique spend model)

| Artifact | What still holds | Gap vs v0.1 |
|----------|------------------|-------------|
| `ScriptMintWitness.v` `asset_from_script` | Option B native RES; unknown → Invalid | No Ed25519, no sighash, still imports `CvgCovenant`, PCORB shape |
| `AssetFromScript.v` | pooling class = `f(script)` | Shape only; not ownership / signature |
| `ScriptShape.v` | created-script admit set | PCORB size gate is not a live spend path |
| `OpaqueBoundary.v` | honest non-claims (no VM, abstract `cm_draw_won`) | Still documents EvalScript as a ghost |
| `ResMintObligation.v` | schedule / H-1 / cap on abstract `cm_draw_won` | Not owner HASH160, not mint sighash, not maturity |
| `spec/rigor_consensus_spec.py` `asset_from_script` | matches classifier | No `SignatureHash` / Ed25519 domain |
| C++ `rigor_sighash_p0_018.json` | tagged preimage + chain_id gold | Not replayed inside Coq |

## Not the production mint rule

Draw, σ, a winner function, and a convergence-root selection digest are not RFC-020. Production minting is `CheckResMintArithmetic` in the C++ node. A Coq file that still states a draw is a statement about that file. It is not a proof of the node. SHA-256d in a Coq hasher is not the production proof-of-work digest. Production proof-of-work is FIPS SHA3-256(`"RGRPOW01" || header_80`).

## MISSING in the old cone (now `rigor_v01/`)

| Property | Old status | New module |
|----------|------------|------------|
| Unique classifier, no second source | partial (`asset_kind_of_script`) | `AssetClassification.v` |
| Script-as-asset injectivity of spendable predicates | missing | `ScriptAssetBoundary.v` |
| Ed25519 unique authorization boundary | missing | `Ed25519Ownership.v` |
| tag \|\| chain_id domain separation | missing | `SignatureDomainSeparation.v` |
| RES_MINT create = spend = HASH160(pk) | old mint count model | `ResMintCorrectness.v` |
| No interpreter constructors | comment-level | `NoInterpreterConsensus.v` |

## Python / Rust

| Layer | Status |
|-------|--------|
| Python L1 (`spec/rigor_consensus_spec.py`) | Classifier aligned. **No** SignatureHash preimage model. |
| Python `python-spec/` | RFC-018 fee-burn / supply layer. Not spend. |
| Rust *formal* model | **Does not exist.** |
| `pool/share-validator/src/res_mint.rs` `is_res_script` | Operational duplicate of the classifier; mint match is looser than C++ unique encoding. Not a proof. |
