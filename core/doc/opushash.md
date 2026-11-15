# OpuHash (Proof-of-Work)

OpuHash is the production proof-of-work digest:

```
OpuHash(header) = SHA3-256("RGRPOW01" || header_80)
```

SHA3-256 is FIPS 202. The domain suffix is `0x06`. Ethereum Keccak-256 uses suffix `0x01` and is not OpuHash. The preimage is the 8-byte ASCII tag `RGRPOW01` followed by the 80-byte header. A bare header hash is not OpuHash.

`CheckProofOfWork` accepts the header when this digest is at or below the target.

The block identifier is SHA256d of the 80-byte header. Merkle roots and txids are SHA256d. Those hashes are not the mining digest.

The full statement, including the negative Keccak-256 vector, is [SPEC-008](../../docs/specification/SPEC-008-PROOF-OF-WORK.md). Compatibility vectors are [RGRPOW01](../../docs/RGRPOW01_COMPATIBILITY.md).

`GetOpusHash` in `core/src/pow_opus.cpp` is a different function. `CheckProofOfWork` does not call it. See [opushash_v3.md](./opushash_v3.md).
