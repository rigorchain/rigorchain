# CPU miner proof-of-work

The in-tree solo miner and the node use the same digest:

```
SHA3-256("RGRPOW01" || header_80)
```

SHA3-256 is FIPS 202. The domain suffix is `0x06`. The miner implementation is `miner/rgrpow.c`. The node implementation is `rigor_kernel::GetRigorPowHash`, called by `CheckProofOfWork`.

The 80-byte header layout is the consensus header serialization. The block identifier remains SHA256d of those 80 bytes. The miner compares the SHA3-256 digest with the target from `nBits`.

`GetOpusHashFromBaseHash` is not this digest. `CheckProofOfWork` does not call it, and `miner/rgrpow.c` does not call it.

Operator notes for the solo binary are in `miner/README_MINER.md`.
