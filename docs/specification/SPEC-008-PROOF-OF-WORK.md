# SPEC-008: RIGOR Proof-of-Work (SHA3-256)

**Document status:** Production specification  
**Historical verification reference:** P0-046  
**Status:** CURRENT PRODUCTION (implemented in `CheckProofOfWork` / `GetRigorPowHash`). **OpuHash** is the algorithm name for FIPS SHA3-256(`"RGRPOW01" || header_80`). `GetOpusHash` in `pow_opus.cpp` is not this digest. `CheckProofOfWork` does not call it.
**Parents:** `SPEC-000`, `SPEC-003`
**Supersedes:** `core/doc/opushash_v3.md`, `core/doc/miner-opushash.md`
**Governance:** Level-5 (frozen architecture change; requires freeze-declaration amendment)

```
equivalence_claimed = false
refinement_proved   = false
```

> **Pre-mainnet only.** This document is ratifiable solely because mainnet has not
> launched. There is no historical chain state, no user funds, and no deployed
> hashrate. After genesis this becomes a hard-fork-level constitutional change.

---

## 1. Scope

This specification replaces the consensus proof-of-work function. It changes
**nothing else**.

| Surface | Disposition |
|---------|-------------|
| PoW digest (per-nonce grinding path) | **Replaced** — SHA3-256, see §3 |
| Block identifier `CBlockHeader::GetHash()` | Unchanged — SHA256d |
| Merkle root, txid, wtxid | Unchanged — SHA256d |
| σ digest, convergence root | Unchanged — SHA256d (`Hash256`) |
| 80-byte header layout, P2P wire, GBT | Unchanged |
| `nBits` semantics, ASERT, `powLimit` | Unchanged (but see §8) |
| Ed25519 spend path, sighash domains | Unchanged |

**Rationale for the boundary.** SHA-256 is removed from the *grinding* path
because a SHA-256-derived PoW grants incumbent SHA-256 silicon a cost advantage
on new hardware. SHA-256 is *retained* for commitments and identifiers because
those are evaluated once per block, are not grindable, and because replacing
them would invalidate `formal_verification/coq/RigorSha256.v` and its enforced
`vm_compute` golden-vector lemmas for no security gain.

---

## 2. Primitive

**SHA3-256 as defined in FIPS PUB 202.** Keccak-f[1600] permutation, 24 rounds.

| Parameter | Value |
|-----------|-------|
| Permutation | Keccak-f[1600], 24 rounds |
| State | 5 × 5 lanes × 64 bits; lane `(x,y)` occupies state bytes `[8·(x+5y), 8·(x+5y)+8)` little-endian |
| Capacity `c` | 512 bits |
| Rate `r` | 1088 bits = **136 bytes** |
| Domain-separation suffix | **`0x06`** |
| Digest length | 32 bytes |

> **`0x06`, not `0x01`.** SHA3-256 (FIPS 202) and Keccak-256 (the pre-standard
> variant used by Ethereum) differ **only** in this suffix byte and produce
> entirely different digests. Selecting the wrong constant in any one consumer
> is a silent chain split. This byte is consensus-critical and MUST be covered
> by the vectors in §7.

---

## 3. Construction

### 3.1 Domain tag

```
RIGOR_POW_DOMAIN_V1 = "RGRPOW01"   // 8 ASCII bytes: 52 47 52 50 4F 57 30 31
```

Declared alongside the existing 8-byte tags in `core/src/script/rigor_tags.h`
(`SighashOruV1`, `SighashResV1`, `SighashMintV1`). Prefix position, one per
domain, same discipline.

### 3.2 Preimage

```
preimage = RIGOR_POW_DOMAIN_V1 ‖ header_80
```

`header_80` is the canonical `CBlockHeader` serialization, unchanged:

| Offset | Width | Field |
|--------|-------|-------|
| 0 | 4 | `nVersion` (LE) |
| 4 | 32 | `hashPrevBlock` (internal byte order) |
| 36 | 32 | `hashMerkleRoot` (internal byte order) |
| 68 | 4 | `nTime` (LE) |
| 72 | 4 | `nBits` (LE) |
| 76 | 4 | `nNonce` (LE) |

**Preimage length is exactly 88 bytes.**

### 3.3 Single-permutation property (normative)

`88 ≤ 135`, so the padded message occupies exactly one rate block, and
`32 ≤ 136`, so the digest is read without a second permutation.

**Computing the RIGOR PoW digest is exactly one invocation of Keccak-f[1600].**

Implementations MAY special-case this fixed shape. Implementations MUST NOT
produce a different result from a general-purpose SHA3-256 over the same 88
bytes.

The padded block `B[0..135]` is:

```
B[0..7]     = RIGOR_POW_DOMAIN_V1
B[8..87]    = header_80
B[88]       = 0x06
B[89..134]  = 0x00
B[135]      = 0x80
```

Because `88 < 135`, the suffix byte and the final `0x80` land in **different**
bytes; the merged `0x86` case of the general padding rule does not arise here
and MUST NOT be implemented for this input length.

Then:

```
state    = B  (as 25 little-endian 64-bit lanes; capacity bytes are zero)
state'   = Keccak-f[1600](state)
pow_digest_bytes = state'[0..31]
```

### 3.4 Digest-to-integer convention

```
pow_digest.begin()[i] = pow_digest_bytes[i]     for i in 0..31
```

Direct byte copy into `uint256`, then compared with `UintToArith256`, which
interprets `uint256` as little-endian. This matches the existing convention for
`CHashWriter::GetHash()` output and requires no byte reversal anywhere.

> Digest-vs-target byte order has already caused one defect in this codebase
> (`scanhash_sha256d`, see `docs/mining/RIGOR_EXTERNAL_STACK_GAPS.md`). Every
> consumer in §6 MUST be pinned by the boundary vectors in §7.2.

---

## 4. Validity predicate

```
valid(header) ⇔ UintToArith256(pow_digest(header)) ≤ target(nBits)
```

with `target(nBits)` decoded exactly as today (`SetCompact`; reject negative,
zero, overflow, or `> powLimit`).

**Genesis exemption is retained unchanged.** `CheckProofOfWork` returns `true`
for `params.hashGenesisBlock` before any digest is computed. Genesis therefore
does **not** require re-mining under this specification; it requires only a
well-formed `nBits`.

---

## 5. Interface change

### 5.1 Not on the CheckProofOfWork path

| Symbol | Location |
|--------|----------|
| `GetOpusHash`, `GetOpusHashFromBaseHash` | Still defined in `core/src/pow_opus.{h,cpp}`. `CheckProofOfWork` does not call them |
| `OPUS_POW_BUFFER_SIZE`, `OPUS_POW_MIX_ROUNDS` | Still defined in `core/src/consensus/rigor_consensus_lock.h`. They are inputs to `GetOpusHash`, not to `GetRigorPowHash` |
| `CheckProofOfWork(uint256 header_hash, ...)` | `core/src/pow.cpp` — **overload deleted (landed)** |

Deleting the `uint256` overload is deliberate. After this change a block
identifier is no longer sufficient to decide proof-of-work, and the type system
must enforce that rather than leaving it to reviewer discipline.

This removal has **already landed**, ahead of the rest of this specification,
because it is safe under OpuHash (which is still a function of the header id)
and because it prevents new call sites from appearing against an API that
cannot survive §3. The body now lives in the `CBlockHeader` overload and
computes `block.GetHash()` internally; the success cache is unchanged. Three
fuzz targets and six unit tests were converted from header ids to headers —
`fuzz/rigor_pow_target.cpp` now drives header *fields* rather than a synthetic
id, and the genesis-exemption tests now use the real genesis header instead of
`consensus.hashGenesisBlock`. All 38 `pow_tests` pass.

Implementers MUST confirm whether `crypto/chacha20` retains any other consensus
consumer once `pow_opus.cpp` is deleted. If not, ChaCha20 leaves the consensus
dependency set entirely.

### 5.2 Added (landed)

```cpp
// core/src/rigor_kernel/pow_digest.h
uint256 rigor_kernel::GetRigorPowHash(const CBlockHeader& header) noexcept;
```

Pure function. No chain context, no locks, no logging, no allocation on the hot
path. This is the first member of the extracted consensus kernel (see §9).

Shipped in `libbitcoin_consensus` as two pairs of files that mirror the Coq
module boundary one-to-one, so the refinement argument has a syntactic
correspondence rather than a narrative one:

| C++ | Coq | Content |
|---|---|---|
| `rigor_kernel/keccak.{h,cpp}` | `RigorKeccak.v` | Keccak-f[1600]; SHA3-256 for one rate block |
| `rigor_kernel/pow_digest.{h,cpp}` | `PowDigest.v` | Domain tag, 88-byte preimage, digest |

Notes that are load-bearing rather than stylistic:

- `Sha3_256Short` **asserts** `len <= 134` instead of falling back to a
  streaming sponge. The merged-padding case (`len == 135`, where the 0x06 suffix
  and the final 0x80 share a byte) is unreachable for an 88-byte preimage, and
  implementing it would add consensus surface nothing needs.
- The domain suffix is `0x06` (FIPS 202), not `0x01` (pre-standard Keccak, as
  used by Ethereum). The two agree on no input.
- `BuildPowPreimage` writes header fields into a stack buffer rather than going
  through `CDataStream`, because it runs once per nonce. It is pinned against
  the canonical serializer by a unit test so the fast path cannot drift.
- The returned `uint256` holds digest bytes in output order, so `UintToArith256`
  reads it little-endian and reproduces the model's `uint256_of_bytes`. Digest
  byte 0 is the least significant limb of the value compared against the target.

`CheckProofOfWork` now calls `GetRigorPowHash`. That switch landed with the
§8.2 constants and the recomputed `hashGenesisBlock`. The miner and pool
consumers in §6 still implement OpuHash and are **not** part of this commit.

### 5.3 Retained

The `CheckProofOfWork` success cache is retained with unchanged semantics: keyed
on block identifier, storing `nBits`, failures not cached. The key remains the
SHA256d block id, which is already computed for every accepted header.

---

## 6. Consumers

The PoW function currently has **independent implementations**. Each is an
opportunity for a silent chain split.

| Consumer | Path | Required action |
|----------|------|-----------------|
| Node | `core/src/pow_opus.cpp` | **Done** — `CheckProofOfWork` calls `rigor_kernel/pow_digest.cpp`. `pow_opus.cpp` remains in the tree for the miner until that consumer switches. |
| Functional tests | `core/test/functional/test_framework/opus_pow.py` | Replace; keep import-time self-test |
| SOLO miner | `miner/opus_hash.{cpp,h}` | Replace; retire `sha2.c` from the PoW path |
| Explorer | `explorer/src/` (`opus_hash` display) | Rename and re-point |

**Requirement.** Ratification of this specification is conditional on reducing
these to **one** shared reference implementation per language runtime, vendored
identically, plus a differential gate (§7.3) that runs every consumer against
the same vector file in CI. A specification alone does not prevent divergence;
the gate does.

---

## 7. Test vectors

### 7.1 Primitive conformance

FIPS 202 SHA3-256 known-answer tests MUST pass in every consumer, including at
minimum the empty message, a single-byte message, and a 135/136/137-byte message
that exercises the block boundary the RIGOR construction itself never reaches.

**Node status: landed** in `core/src/test/rigor_pow_digest_tests.cpp` (empty,
`abc`, and the 134-byte maximum this construction accepts). The 135/136/137
cases do not apply to the node, which asserts rather than implements them
(§5.2). Remaining consumers in §6 are still outstanding.

### 7.2 Construction vectors

Frozen vectors MUST pin, at minimum:

1. All-zero 80-byte header.
2. The ratified genesis header.
3. A header whose digest is immediately below its target (accept boundary).
4. A header whose digest is immediately above its target (reject boundary).
5. A header differing from (1) only in `nNonce`, proving nonce sensitivity.
6. A `Keccak-256` (`0x01` suffix) digest of the same preimage, recorded as a
   **negative** vector, so any implementation that selects the wrong suffix
   fails loudly rather than silently.

Stored under `core/src/test/data/` and mirrored into
`formal_verification/phase3_golden_vectors.json`.

**Node status:** (1), (5) and the domain-tag negative case are landed, and (1)
is confirmed byte-identical across three independent implementations — the Coq
model by `vm_compute`, Python `hashlib`, and the C++ kernel:

```
zero header → dd 05 7f c1 df 8f 5b 94 … d6 de 04 fc
            → uint256 "fc04ded6…c17f05dd"
            → 113991443214250993822008262451400111709481499196443645548532729518767640348125
```

A vector with **every header field distinct** was added beyond the list above.
It is not optional: an all-zero header cannot detect a field-ordering or
endianness bug, because every permutation of zeros is zero, so vector (1) alone
would pass against a wrong serialization.

Still outstanding: (2) the genesis header, which cannot be pinned until §8.2
lands and `hashGenesisBlock` is recomputed; (3) and (4) the target accept/reject
boundaries; (6) the `Keccak-256` `0x01`-suffix negative vector.

### 7.3 Differential gate

Extend `formal_verification/tests/test_hash_kernel_tripartite.py` to cover
SHA3-256 across: extracted Coq kernel, portable C++, and Python. Extend the
existing `run_differential_gate.sh` to additionally exercise the miner C and
pool Rust implementations. A disagreement fails the build.

---

## 8. Difficulty re-derivation (required)

### 8.1 Measured cost ratio

Work per nonce falls by two to two-and-a-half orders of magnitude:

| | Per-nonce cost |
|---|---|
| `GetOpusHash` (not the production digest) | ≈ 1024 ChaCha20 block operations + ≈ 518 SHA-256 compressions |
| Production OpuHash | 1 Keccak-f[1600] permutation, FIPS SHA3-256 domain suffix `0x06` |

The ratio was measured rather than estimated. Both sides were timed in one
process, one thread, on one idle machine, linking the tree's real
`GetOpusHashFromBaseHash` (so the `GetOpusHash` side includes its SHA-NI accelerated
SHA-256) against a SHA3-256 that implements the §3 construction exactly — one
rate block, one permutation, no streaming API.

| Implementation | Rate (1 thread) | R = rate / OpuHash rate |
|---|---|---|
| OpuHash (tree code) | 3,173 H/s | 1 |
| **`rigor_kernel::GetRigorPowHash` (shipped)** | **368,214 H/s** | **117 (2^6.9)** |
| SHA3-256, standalone scalar C | 366,321 H/s | 115 (2^6.8) |
| SHA3-256, OpenSSL 3.0 asm | 997,605 H/s | 314 (2^8.3) |

The shipped kernel was measured through its real entry point (header in,
`uint256` out), so header serialization is included exactly as a miner pays for
it. It lands within 0.5% of the standalone scalar loop, which confirms the
serialization fast path costs nothing measurable.

Two calibration notes:

- The measured OpuHash rate is 3,173 H/s × 8 threads ≈ 25.4 kH/s, against the
  28 kH/s that `GenesisLockMainnet::NBITS` was originally calibrated against.
  The bench machine is therefore within ~10% of the reference machine, so these
  numbers are directly comparable to the existing constant.
- **R = 314 is a lower bound.** The OpenSSL figure still carries per-call `EVP`
  dispatch overhead, and a miner links the permutation directly.

**Calibrate on the miner's implementation, not the node's.** The shipped kernel
is a portable scalar loop and is **2.7× slower than OpenSSL's hand-written
assembly** for the identical function. Difficulty must be set for the code that
actually competes for blocks. Calibrating on R = 117 would make blocks arrive
roughly 2.7× too fast the moment anyone links an optimized Keccak, which is a
one-line change for a miner and mandatory for an ASIC. This gap is a
calibration input, not a defect: §10 already states the PoW is
hardware-friendly by design, so assuming the best available implementation is
the correct posture rather than a pessimistic one.

**Adopted value: R = 2^9 = 512.** This is the next power of two above the
measured lower bound, and it is the value at which an optimized SHA3-256
reproduces the original design intent of ~10 minutes for the first block on a
typical 8-thread PC.

### 8.2 Derived constants (mainnet)

Every constant whose justification is stated in units of *hashes* moves by the
same factor R, because the wall-clock cost of a hash fell by R.

| Constant | OpuHash baseline | Mainnet |
|---|---|---|
| `GenesisLockMainnet::NBITS` | `0x1e010000` (target 2^232, work 2^24) | `0x1d008000` (target 2^223, work 2^33) |
| `consensus.powLimit` | `0000ffff…` (2^240−1, work 2^16) | `0000007fff…` (2^231−1, work 2^25) |
| `consensus.nMinimumChainWork` | 2^26 | 2^33 − 1, genesis work (§8.3.1; was 2^35) |

Expected trials for the first block rise from 1.678e7 to 8.590e9.

Mainnet `nBits` is `0x1d008000`. `GenesisLockMainnet::TIMESTAMP` is `1792908000`
(2026-10-25 06:00:00 UTC). Block identity is SHA256d of that 80-byte header:

| Field | Value |
|---|---|
| `hashGenesisBlock` | `0xe2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468` |
| `hashMerkleRoot` | `0x9d3fbd207af927fc8e2c57e975d8d5845913bf059d956592e8a0bcf38b3515dd` |

Merkle root is the hash of the coinbase transaction.
`defaultAssumeValid` is pinned to `hashGenesisBlock` and moves with it.
`core/doc/genesis-lockdown.md` records the frozen table.

Genesis itself is PoW-exempt (`header_hash == params.hashGenesisBlock` returns
early), and `NONCE = 0`. `NBITS` is therefore the *seed difficulty for block 1*,
not a mined proof, and no genesis re-mining is required.

ASERT parameters (`spacing = 600s`, `τ = 43200s`) do not change. The half-life is
`MAINNET_POW_ASERT_HALF_LIFE` in `core/src/consensus/rigor_consensus_lock.h`
(static-asserted); an implementation using any other τ computes different `nBits`
from height 2 onward.

### 8.3 Headers-sync gate (resolved)

While re-deriving the above, the stated justification for `nMinimumChainWork`
was found to be **false already, under OpuHash**. `kernel/chainparams.cpp`
claims:

> 2^26: four genesis-difficulty blocks (0x1e010000 ≈ 2^24 work) pass the
> headers-sync gate; a full 2000-header powLimit batch (2000 × 2^16) does not.

The arithmetic does not support the second clause:

| Quantity | Current | Under R = 2^9 |
|---|---|---|
| 4 × genesis work | 2^26.00 | 2^35.00 |
| 2000 × powLimit work | 2^26.97 | 2^35.97 |
| Gate | 2^26 | — |

A 2000-header `powLimit` flood carries **more** work than four genesis-difficulty
blocks (2^26.97 > 2^26), so it passes the gate today. Scaling both sides by R
preserves the defect exactly, because the genesis/powLimit target separation
stays at 2^8 while the claim needs a separation above 2000/4 = 500 ≈ 2^9.

**Resolution: delete the claim, do not re-derive around it. The invariant is
vacuous, not merely mis-stated.**

The gate compares work to work. A header contributes only the work it carries,
and producing a header that passes the PoW check at all costs exactly that much
hashing — an attacker cannot manufacture headers more cheaply than the work they
carry. Total attacker cost is therefore identically the total work contributed,
so clearing the gate always costs `nMinimumChainWork` hashes regardless of how
many headers it is split across, how easy each one is, or how many p2p messages
carry them.

`HeadersSyncState` makes this concrete: a peer stays in `PRESYNC`, costing one
bit of memory per header, until its chain clears the gate. Nothing is committed
before the work is demonstrated, and per-header presync cost does not vary with
header difficulty.

"2000 headers" is `MAX_HEADERS_RESULTS`, the size of one p2p message. An
attacker is not limited to one message, so this was never a security boundary —
it is not a unit in which any property of this gate can be expressed.

Both options previously floated here are therefore rejected:

- Raising the floor to 2^37 would buy nothing except a longer launch warm-up.
- Moving `powLimit` to 2^232 would satisfy the letter of the claim by making
  minimum-difficulty headers *cheaper* (work per header 2^24 instead of 2^25),
  which weakens the very DoS property the gate exists to provide. Satisfying an
  invariant by degrading what it was meant to protect is the wrong trade.

**Adopted:** `nMinimumChainWork = 2^35`, which is four genesis-difficulty blocks
under §8.2, preserving both the existing block-count semantics and the
wall-clock attacker cost:

| | OpuHash (current) | SHA3-256 (proposed) |
|---|---|---|
| Gate | 2^26 | 2^35 |
| Honest blocks to reach it | 4 | 4 |
| Attacker hashes required | 2^26 | 2^35 |
| Wall clock, 8-thread PC | ≈40 min @ 28 kH/s | ≈36 min @ 16 MH/s |

The false clause has been removed from `kernel/chainparams.cpp` ahead of this
specification, since it misdescribed the *current* OpuHash constants. The
replacement comment states the work-cost invariant and explicitly warns against
reintroducing a header-count one.

#### 8.3.1 Launch value amended to genesis work (supersedes the 2^35 adoption)

`nMinimumChainWork` is now the chainwork of the genesis block alone:
`GetBlockProof(0x1d008000)` = `2^256 / (2^223 + 1)` rounded down = `2^33 − 1` =
`0x1ffffffff`. This is a header-sync policy parameter, not a consensus rule;
no block valid before is invalid now or vice versa.

The 2^35 floor was above the work of any chain shorter than height 4
(`4 × (2^33 − 1) = 2^35 − 4`). A node whose tip is below the floor:

- stays in initial block download regardless of tip age;
- answers `getheaders` with an empty `headers` message;
- treats every peer's headers as below the anti-DoS threshold
  (`max(tip − 144 blocks, nMinimumChainWork)`), so they enter `PRESYNC` or are
  ignored, and ignores compact blocks below the same threshold;
- disconnects outbound peers whose headers chain is below the floor while in IBD.

At launch every node is in that state, so heights 1–3 could not propagate:
each miner extended a private chain under `-solomining`, and the first to reach
height 4 alone replaced everyone else's blocks. That is a designated-first-miner
effect created by a parameter, which the launch rules exclude.

At genesis work, a genesis-only node is not below the gate, block 1 already
carries more work than the gate, and the 144-block tip-relative window becomes
the binding anti-DoS bound from height 145. Attacker cost is unchanged in kind:
headers still cost exactly the work they carry; the smallest batch that clears
the gate drops from 2^35 to 2^33 hashes for the first ~148 blocks only. ASERT
cannot cheapen headers in that window, because lowering difficulty requires real
elapsed time behind schedule (half-life 12 hours) and timestamps are bounded by
`MAX_FUTURE_BLOCK_TIME`.

This is the launch value only. Raise it toward canonical tip chainwork in the
first release after launch, as the release policy in `chainparams.cpp` requires.

### 8.4 Gate

The production implementation now exists (§5.2 landed) and R has been
re-measured against it; the row is in the §8.1 table. What remains open before
ratification:

- ~~`RIGOR_GENESIS_DIFFICULTY_STARTUP_TIME_AUDIT.md` MUST be re-run~~ **Done.**
  That audit turned out to model difficulty *dynamics after launch*, not time to
  mine the first block, and its ASERT analysis references no PoW function. Its
  one constant-dependent figure (~47 steps from genesis difficulty to powLimit)
  depends only on the genesis/powLimit *ratio*, which §8.2 preserves at 2^8. A
  supplement recording this is appended to that document; no re-derivation was
  needed. An earlier draft of this section claimed the audit was written in
  OpuHash trials, which overstated the work required.
- R = 2^9 assumes miners link an optimized Keccak. If the project instead
  decides to ship an optimized permutation in the node, re-measure and re-derive
  rather than keeping 2^9 by inertia.

---

## 9. Formal model

OpuHash is **not** modeled in Coq today; the PoW predicate is currently an
opaque C++ function outside the verified boundary. This specification brings it
inside.

| Module | Content |
|--------|---------|
| `formal_verification/coq/RigorKeccak.v` | Executable Keccak-f[1600]; θ, ρ, π, χ, ι; 24 round constants; 25 rotation offsets |
| `formal_verification/coq/PowDigest.v` | The fixed-shape §3.3 wrapper and `pow_digest` |

Both discharge their vectors by `vm_compute`, matching the discipline already
used for `RigorSha256.v`. `Print Assumptions` MUST report
`Closed under the global context` for every new theorem — no hash oracle, no
axiom.

**Status: both modules are written and compiled** (Coq 8.15, registered in
`_CoqProject` and `_CoqProject.protocol-core`). All 11 theorems report
`Closed under the global context`.

**Performance note (spike closed).** Lanes are represented as `N` with an
explicit 64-bit mask; boolean-list lanes were rejected. The spike passed with
large margin: `RigorKeccak.v` compiles in 1.4 s including three full
Keccak-f[1600] evaluations (FIPS 202 vectors for the empty message, `abc`, and
a 134-byte block-boundary probe), and `PowDigest.v` in 1.7 s. The permutation is
written as an explicit 25-lane destructuring rather than `nth` indexing, so the
ρ/π offset table is auditable against the standard line by line.

### 9.0 Reduction boundary (load-bearing)

The model is an **executable oracle on concrete inputs, not a symbolic
rewriting target**. Any goal that forces weak-head reduction of
`uint256_of_bytes (rigor_pow_digest h)` for a *symbolic* `h` makes Coq's
conversion machine diverge: exposing a constructor drives the evaluator into
`keccak_f1600`, which is 24 nested applications of a 25-way `match`, and with
`h` symbolic `lanes_of_bytes` gets stuck on the 9th byte so no `match` ever
fires while every enclosing round re-evaluates the stuck argument.

Measured on this tree: `lia`, `exact`, and `Qed` each ran past 200 s and were
not interruptible by `Timeout`. All three are the same root cause. `generalize`
over the digest does not help, because the kernel re-checks the instantiated
term at `Qed`.

Consequences, which constrain §7.3 and §9.1:

- Statements about `length` are fine (they never force the byte contents).
- Statements over an *abstract* digest are fine; this is why monotonicity in the
  target is stated as `target_ok_monotone` over an abstract `digest` rather than
  over `pow_valid` applied to a symbolic header. Monotonicity is a property of
  the comparison, not of Keccak.
- `vm_compute` on a *concrete* header is total and fast.

**The differential harness MUST therefore be built on concrete vectors.** A
symbolic equivalence proof against the C++ implementation is not reachable by
this route, and no plan should assume it is.

### 9.1 Refinement pilot

`pow_digest` is a total function of 88 fixed bytes with no external
dependencies, no chain context, and no performance-critical caller. It is
therefore the intended pilot for the extract-and-link refinement path:

```
Coq PowDigest.v → extraction → linked kernel → node calls it directly
```

If the pipeline closes here, the same pipeline applies to `classify` and
`supply_delta`, which already have extracted kernels (`classify_kernel.ml`,
`supply_kernel.ml`) and differential CLIs but are still shadowed by hand-written
C++.

Only after that link exists may `equivalence_claimed` move from a global
`false` to a scoped form. Until then it stays `false`.

---

## 10. Mining policy statement (normative for public copy)

RIGOR's proof-of-work is a single SHA3-256 permutation, optimized for hardware
implementation. The protocol does **not** provide and does **not** pursue ASIC
resistance. Dedicated hardware is an expected outcome, not a failure mode.
RIGOR makes no mining-decentralization promise and does not treat CPU mining as
a long-term path; the CPU client is a bootstrap and test tool. Chain security
budget is maintained by the perpetual ORU tail subsidy, not by hardware class.

The one property this choice does buy, and the only one that may be claimed: no
incumbent manufacturer holds a silicon head start, so the hardware market begins
at parity. This is a supply-diversity argument. It is not a decentralization
claim.

Copy that implies otherwise — "CPU-friendly", "fair mining", "narrows the CPU vs
GPU gap", "ASIC-resistant" — MUST NOT appear in any RIGOR surface. See
`docs/release/RIGOR_PUBLIC_SYSTEM_POSITIONING.md` §4.

---

## 11. Authority order

When documents disagree:

```
rigor_tags.h + rigor_kernel/pow_digest.cpp
  → formal_verification/coq/PowDigest.v
  → this specification
  → MAINNET_PROTOCOL_FREEZE_DECLARATION.md (amended)
  → whitepaper / narrative
```

---

## 12. Ratification checklist

- [x] Coq `RigorKeccak.v` + `PowDigest.v` build; FIPS 202 KATs pass by `vm_compute`; zero axioms
- [x] `vm_compute` performance spike acceptable (1.4 s / 1.7 s; `N` lanes with explicit 64-bit mask)
- [x] `rigor_kernel/pow_digest.{h,cpp}` landed and agrees with the Coq model on the §7.2(1) vector
- [ ] `rigor_kernel/pow_digest.{h,cpp}` agrees with *extraction* on every §7 vector (extraction not yet wired)
- [x] `CheckProofOfWork(uint256, ...)` overload deleted; tree builds
- [ ] `pow_opus.{h,cpp}` and both OpuHash constants deleted
- [ ] ChaCha20 consensus dependency re-audited
- [ ] All six consumers in §6 converge on one implementation per runtime
- [ ] Differential gate covers all consumers, including the §7.2(6) negative vector
- [x] Genesis difficulty re-derived; startup-time audit re-run (ASERT analysis unchanged; constants scaled by R = 2^9)
- [ ] §10 policy statement propagated to miner README, mining page, positioning doc
- [ ] `opushash_v3.md` and `miner-opushash.md` deleted or replaced
- [ ] Freeze declaration amended (Level-5)
