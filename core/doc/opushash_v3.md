# OpuHash

Production proof-of-work is FIPS SHA3-256(`"RGRPOW01" || header_80`).

`CheckProofOfWork` in `core/src/pow.cpp` calls `rigor_kernel::GetRigorPowHash`. The preimage is the 8-byte ASCII tag `RGRPOW01` followed by the 80-byte header. The domain suffix inside SHA3-256 is `0x06`. The block identifier is SHA256d of that same 80-byte header. The identifier is not the mining digest.

The full statement is [SPEC-008](../../docs/specification/SPEC-008-PROOF-OF-WORK.md). The short statement is [opushash.md](./opushash.md). The in-tree miner uses the same digest (`miner/rgrpow.c`).

`GetOpusHash` and `GetOpusHashFromBaseHash` are defined in `core/src/pow_opus.cpp`. `CheckProofOfWork` does not call them. They are not the production mining digest.
