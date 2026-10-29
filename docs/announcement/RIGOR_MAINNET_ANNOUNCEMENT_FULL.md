# [ANN] RIGOR Mainnet — Proof-of-Work UTXO chain with two native consensus assets (ORU + RES)

RIGOR Mainnet activation time is fixed at **2026-10-25 06:00:00 UTC**. This post states why the protocol was built, what architecture it chose, how its two assets are defined in consensus, how the chain is launched, and how to run a node or mine. Every number below is taken from the released consensus code and the protocol specifications, both of which are linked at the end.

The one structural fact worth understanding before anything else: **RIGOR has two native assets, and one of them cannot be mined.** ORU is produced by Proof-of-Work. RES is produced only inside a transaction that destroys ORU, in the same transaction, under an integer inequality evaluated by every full node. There is no coinbase RES, no swap contract, no issuer, and no reverse path from RES back to ORU.

---

## 1. Why RIGOR exists — the design question

Many digital monetary systems ask one native asset to serve several functions at once:

```
medium of exchange
miner incentive
network security budget
long-term value storage
```

These functions interact. An asset optimized for everyday exchange tends toward frequent circulation. An asset optimized for long-term reserve characteristics tends toward reduced velocity and scarcity preservation. Security is also a continuous flow — hashpower protects all prior history, not only the current block — while a transaction fee is a one-time payment for inclusion. Whether fee markets fund security after a subsidy ends remains an open empirical question in Proof-of-Work research, not a settled result.

Bitcoin chose a unified approach: one native asset coordinating all monetary functions. That is a coherent and historically significant design decision, and RIGOR does not consider Bitcoin's design principles incorrect. RIGOR is built on properties Bitcoin demonstrated:

- decentralized transaction ordering through Proof-of-Work
- UTXO-based ownership
- independent node verification
- deterministic, fixed consensus rules

The difference is not the foundation. The difference is the allocation of monetary roles. RIGOR exists to explore one question:

> Can monetary roles be separated at the protocol layer while preserving the fundamental properties of Proof-of-Work and UTXO-based consensus?

This is a design-space exploration, not a verdict on any other system. RIGOR is not presented as a replacement for Bitcoin and not as a competitor to programmable chains.

---

## 2. The architecture RIGOR chose — the design answer

Separating monetary roles is easy if you are willing to add machinery: a second ledger, account balances, an asset registry, or contract-mediated conversion. RIGOR's central design constraint is the opposite one:

> Separate monetary roles without introducing a parallel accounting system.

The principle the protocol follows is therefore:

> Add monetary distinction, not consensus complexity.

Concretely, the architecture is:

**One UTXO state space.** There is no second ledger, no account model, no `(asset_id, amount, script)` triple, and no separate ORU and RES databases. The fundamental state object remains the Bitcoin object. A coin is `(nValue, scriptPubKey)` identified by `(txid, n)`.

**Script-as-asset.** Asset identity is determined by the locking script itself, as a consensus function of bytes — not by wallet interpretation, explorer metadata, an address prefix, or an indexer database. The same bytes produce the same classification on every node. The rule is: identity lives in the script, legitimacy lives in consensus.

**Closed templates, failing closed.** There is no general-purpose script VM. Consensus recognizes a fixed template set; everything else is rejected rather than interpreted. An unknown witness version does not become spendable, and an unknown script does not become an implicit asset. A future asset type would require an explicit consensus change.

**Dual-pool conservation in one state space.** An output carries exactly one asset identity. ORU inputs cannot fund RES outputs and RES inputs cannot fund ORU outputs. The only exception is the one-way destroy-to-mint path below.

**Domain-separated Proof-of-Work.** Block identity and Proof-of-Work validity are separate concerns. Block identity stays `SHA256d(header_80)`; mining validity uses `SHA3-256("RGRPOW01" || header_80)`. Mining produces shared ordering. Mining does not produce RES.

**A one-way monetary bridge.** The transformation from circulation asset to reserve asset happens inside a single consensus transition, with the destroy visible on the ledger, and it cannot be reversed.

The result is that the three roles are assigned to two coupled native assets rather than to one:

```
work                       →  ORU subsidy, 128 per block, independent of RES demand
RES demand at the cost     →  ORU destroyed        (RFC-020)
RES transfers              →  half the ORU fee destroyed (RFC-018)
```

The miner budget has a protocol floor of 128 ORU per block that does not depend on long-term holders transacting. The protocol does not create demand for RES; it defines the transformation that converts such demand, if it exists, into destroyed ORU. Whether 128 ORU per block is a sufficient security budget in external purchasing-power terms is a market outcome, not a protocol rule.

What this architecture deliberately does not provide:

- no general-purpose virtual machine and no smart-contract execution layer
- no arbitrary token issuance and no token registry
- no account-based global state
- no colored coins, inscriptions, wrapped assets, or overlay protocols
- no governance layer inside consensus: no privileged issuer, no administrator key, no automatic monetary parameter adjustment

The stated trade-off is a smaller consensus surface at the cost of programmability. A closed monetary surface is a poor fit for applications that need general computation or contract-mediated conversion, and those belong on systems designed for them. The architecture does not ask participants to trust it. It asks them to inspect it.

---

## 3. The chain, mechanically

```
Consensus            Proof-of-Work, longest/most-work chain
Ledger               UTXO only (no account model, no global asset state)
PoW validity hash    OpuHash = SHA3-256("RGRPOW01" || 80-byte header)
Block identity hash  SHA256d of the same 80-byte header
Difficulty           ASERT (aserti3-2d), 600 s target spacing,
                     43200 s half-life, anchor height 1, ±12.5% per-block clamp
Signatures           Ed25519, fixed 64-byte signatures
Script               Four closed templates, no script virtual machine
Maturity             100 confirmations (coinbase ORU and RES mint outputs)
```

The four templates, and the classification each produces:

```
00 14 <20-byte HASH160(Ed25519 pubkey)>      → ORU        (address rg1q…)
51 20 <32-byte Ed25519 pubkey>               → RES        (address rg1p…)
14 <20-byte HASH160> bd                      → ORU destroy (never enters the UTXO set)
6a <pushes>, nValue = 0                      → data, no asset
anything else                                → rejected as a monetary output
```

Production RIGOR has no `EvalScript`, no opcode interpreter, and no `interpreter.cpp`. `CScript` is a byte container; spends are checked by a structured predicate (`VerifyRigorPredicate`). P2PKH, P2SH, P2WSH, bare multisig, Taproot and secp256k1 spend paths are not accepted. Witness v1 in RIGOR is a 32-byte Ed25519 program; it is not Bitcoin Taproot.

---

## 4. The two assets

**ORU** — 8 decimals. It pays transaction fees, it is the block subsidy, and it is the only asset that can be destroyed to create RES.

```
subsidy(0)      = 0 ORU
subsidy(h ≥ 1)  = 128 ORU    for every height, with no halving
```

ORU has no declared terminal supply cap. `MAX_MONEY` in the code (10^10 ORU) is an arithmetic sanity bound on amounts, not a monetary cap. ORU's issuance *rate* decays as 1/t; its residual inflation *level* is `1 − ρ`, where `ρ` is the fraction of all emitted ORU that is ever destroyed to create RES. If `ρ` were 100%, circulating ORU supply would be constant.

**RES** — 8 decimals. It is the asset to which the protocol assigns the scarcity role. It is not mined, not issued by governance, not swapped 1:1, and not convertible back to ORU. `S_RES(0) = 0`; every RES atom that will ever exist has to pass through the mechanism in the next section.

Bitcoin uses a single native monetary asset. RIGOR defines two, with the subsidy paid in one of them and the scarce stock created only by destroying that same subsidy asset.

---

## 5. Convergence: how RES comes into existence

A user spends ORU into a single `OP_CONVERGE` output of amount `B`. That output is classified as ORU and is **never added to the UTXO set** — the ORU is gone. The same transaction may create Witness v1 RES outputs summing to `R` atoms. At the confirming height `h`, every node evaluates:

```
R > 0   →   R · (2^25 + h)^2  ≤  B << 40
R = 0   →   B unconstrained          (a plain destroy that mints nothing stays legal)
```

Three properties of this are worth reading carefully:

- **The destroy is visible**, as a distinct 22-byte output with an explicit amount. It is not inferred from `inputs − outputs − fee`. If destroy and mint sat in different transactions, the ledger would contain a period where credit exists without visible backing.
- **The mint is same-transaction.** `R` is the sum of the Witness v1 outputs in that body transaction. **The miner who includes the transaction receives no RES.** The RES goes to the outputs the transaction itself specifies. Pool policy cannot allocate RES, and changing a payout address cannot create RES.
- **The predicate is integer.** Same `(R, B, h)` produces the same accept-or-reject for everyone. There is no rate oracle, no hidden parameter, no rounding direction; consensus performs one squaring, one multiplication and one shift, and never divides.

The equivalent display form of the same rule is the protocol destroy cost:

```
destroy_cost(h) = 1024 · (2^25 + h)^2 / 2^50   ORU destroyed per whole RES
destroy_cost(0) = 1024 ORU exactly
```

The smallest destroy that mints one RES atom at height 0 is 1024 ORU atoms (0.00001024 ORU). The cost is strictly increasing with height — about +0.000089% per block — so it is a pure function of when the transaction confirms, not of who submits it. `destroy_cost(h)` is a protocol destroy requirement, not a market quote.

There is no per-block mint cap, no minting horizon, and mint outputs carry 100-confirmation maturity as reorg protection. Relevant rejection codes are `bad-txns-res-mint-underpaid`, `bad-txns-res-mint-no-burn`, and `illegal-res-to-oru-conversion`.

---

## 6. Where 4,194,304 RES comes from

The RES ceiling is `RES_MAX_SUPPLY = 419,430,400,000,000` atoms = **4,194,304 RES = 2^22**. This number is not a chosen supply cap. It is the closed-form integral of the per-block mint ceiling, and it is the product of three protocol parameters:

```
S  = 2^7  = 128 ORU        block subsidy
P0 = 2^10 = 1024 ORU       destroy cost for 1 RES at height 0
H  = 2^25                  hardening timescale (blocks)

Σ_{h≥0} S / destroy_cost(h)  =  (S / P0) · H  =  2^7 / 2^10 · 2^25  =  2^22
```

Because the exponent in `destroy_cost(h)` is exactly 2, the integral closes with no residual term, which is why the bound lands *on* a power of two. The same relationship forces the consensus shift: `40 = 50 − log2(1024)`. Change the genesis destroy cost and the shift moves with it; the three constants cannot be edited independently without breaking a visible identity that is asserted at compile time in the released code.

A consequence of `destroy_cost(h)` being strictly increasing is that delaying a destroy always yields strictly less RES per ORU. The ceiling therefore needs **no consensus counter**: no strategy can approach it from above, and block validation never consults a cumulative supply table. Cumulative issuance also has a closed form: `cumulative(h) = 4,194,304 · h / (h + 2^25)`.

Minting has no expiry parameter. It ends only when a full block's subsidy no longer buys a single RES atom — a consequence of the arithmetic, at height ≈ 118,599,277,598.

---

## 7. Issuance ceiling over time, and what it is not

At 600-second target spacing, a year is about 52,594 blocks, so the ORU subsidy emits about 6,732,032 ORU in the first year. The table below asks what would happen if **every** ORU emitted were destroyed for RES — that is `ρ = 100%`. Both RES columns are therefore **upper bounds**, printed by the numeric specification (`tools/res_continuous_price_schedule.py`), not forecasts:

```
Year   Height        ORU per RES   Cumulative RES   % of ceiling      (all at ρ = 100%)
   1       52,594        1,027.21            6,564         0.16%
   4      210,379        1,036.88           26,134         0.62%
  10      525,949        1,056.35           64,729         1.54%
  16      841,518        1,076.01          102,616         2.45%
  32    1,683,037        1,129.30          200,331         4.78%
 100    5,259,492        1,370.17          568,350        13.55%
 638   33,555,558        4,096.14        2,097,187        50.00%
```

Two protocol facts are visible in it. First, issuance reaches half the ceiling at year 638, the height where `h = 2^25`; the pole is the median of the `ρ = 100%` distribution. Second, the destroy cost moves slowly at the start: 1,024 ORU per RES at height 0 rises only about 5% across the first sixteen years. The curve is strictly increasing, but it is not a steep early ramp.

These are four different quantities and this announcement will not blur them:

- **Theoretical maximum:** 4,194,304 RES, derived in section 6. It is an upper bound on a demand-driven process.
- **Actual minted supply:** `ρ · 2^22`. If nobody converges, RES issuance is zero and the chain still functions.
- **The table above:** the `ρ = 100%` boundary of that range, which requires that every emitted ORU is destroyed. Any lower participation produces proportionally less.
- **Circulating supply:** whatever subset of minted RES sits in spendable UTXOs.

The protocol enforces the mint predicate, the absence of a RES→ORU path, and the ceiling. It does not enforce, promise, or predict that anyone destroys ORU, that the ceiling is ever approached, that a market exists, or that `ρ` lands in any particular range. Mathematical scarcity here is a protocol property, and nothing more than that.

RFC-020 also records the open question in this area rather than hiding it: RES issuance is thin in the early years, and a market too thin to discover any RES/ORU ratio at all would prevent demand from being transmitted into ORU destruction. The RFC asserts the mechanism, not a threshold.

---

## 8. The second ORU sink (RFC-018)

On transactions whose inputs are RES, `floor(fee / 2)` of the ORU fee is not claimable by the coinbase and therefore never enters the UTXO set. Pure ORU spends and Convergence transactions are excluded — the Convergence destroy is already a destroy. Fee identity is unchanged (`fee = ORU_in − ORU_out`); only the miner's claimable amount is reduced, which keeps an incentive to include RES transfers. Genesis-active, no activation gate. Overclaiming is consensus-invalid; underclaiming remains legal as a voluntary extra destroy.

---

## 9. Verification boundary

RIGOR maintains a Coq model of its consensus kernel. The project publishes exactly one sentence about it, and this announcement repeats it without shortening it:

> Inside the Coq consensus kernel model, critical consensus invariants have been machine-checked, with explicit extraction and runtime trust boundaries.

What that does **not** say:

```
formal verification of the protocol model
        ≠  equivalence with the running C++ implementation
        ≠  a proof about the running binary
```

The repository carries `equivalence_claimed = false` and `refinement_proved = false` as explicit flags. Hash functions are uninterpreted in the model; Ed25519 and SHA3 security are not proved there; `ActivateBestChain` and `ConnectBlock` refinement are outside the proved perimeter. Explorers, websites and dashboards display public data — validity is decided only by a full node recomputing the same bytes.

---

## 10. Launch conditions

RIGOR Mainnet activation time is fixed at **2026-10-25 06:00:00 UTC**.

The network does not activate before this UTC time.

```
Mainnet genesis   e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468
Merkle root       9d3fbd207af927fc8e2c57e975d8d5845913bf059d956592e8a0bcf38b3515dd
Genesis nTime     1792908000  (2026-10-25 06:00:00 UTC)
Genesis nBits     0x1d008000  (starting difficulty for height 1; Genesis is PoW-exempt)
Genesis nNonce    0
Genesis message   "Selection not computation. Convergence defines state. Naming is consensus."
```

The Genesis block is the fixed chain identity anchor for Mainnet. Its timestamp is set to the Mainnet activation time, but Genesis itself is not a mined Proof-of-Work block. `nNonce` remains 0, and `nBits` defines the starting difficulty for height 1.

The concrete distribution facts, each verifiable from the Genesis block and the released source:

- **Block 0 subsidy is 0 ORU.** The Genesis coinbase output has `nValue = 0` and is not added to the UTXO set.
- **Block 1 subsidy is 128 ORU**, identical to every later block. There is no early-bonus phase, no decaying schedule, and no halving.
- **ORU can only come into existence as a coinbase output at height ≥ 1.** Consensus contains no other issuance path: no allocation output, no founder script, no administrative key, no premined UTXO set.
- **RES starts at zero and can only be created by Convergence.** Coinbase cannot mint RES; the coinbase scriptSig is a BIP34 height push and pays ORU only.
- **The first subsidy-bearing block is height 1.** It can be produced by public Proof-of-Work only after Mainnet activation time. Height 0 does not pay miners.

---

## 11. Network parameters

```
Chain name                 RIGOR
Magic bytes                ce f1 db fa
P2P port                   8760
RPC port                   8761   (localhost only)
Bech32 HRP                 rg     — ORU addresses rg1q… , RES addresses rg1p…
Sighash tags               RGRORU01 (ORU) / RGRRES01 (RES); sighash binds chain_id = genesis hash
DNS seed                   seed.rigorchain.com
Mainnet activation time    2026-10-25 06:00:00 UTC
Genesis                    e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468
Release                    v0.2 — Mainnet (Windows x86_64, Linux x86_64; no macOS build in this release)
```

A node that reports any other block-0 hash is not on this Mainnet.

---

## 12. Mining

Coinbase pays ORU only. Proof-of-Work does not create RES, and no pool policy, payout address or coinbase edit can allocate RES.

The documented public participation model in this release is **solo mining against your own node**:

```
rigord getblocktemplate → copy the canonical coinbase bytes → SHA3-256("RGRPOW01" || header_80) nonce search → submitblock
```

The node is the only consensus assembler. `getblocktemplate` returns `default_coinbase_tx`, and the miner copies those bytes rather than rebuilding subsidy or scriptSig; the grindable fields are the nonce and the supplied `curtime`. `getwork` is not a RIGOR path, and Stratum is refused as a solo URL. The repository contains pool software, but this launch does not operate or document a public pool.

The release includes a reference CPU miner (`rigor-miner.exe` / `rigor-miner`) that computes the same SHA3-256 `RGRPOW01` digest as the node's `CheckProofOfWork`. No GPU or ASIC implementation is published. OpuHash is the algorithm name; `rgrpow01` is the miner `-a` identifier. They are the same function, not two algorithms.

Windows:

```
rigor-miner.exe -a rgrpow01 -o "http://127.0.0.1:8761" -u "<rpcuser>" -p "<rpcpassword>" --coinbase-addr="rg1q…" -t <threads>
```

Linux:

```
tar -xzf rigor-v0.2-miner-linux-x86_64.tar.gz
./bin/rigor-miner -a rgrpow01 -o http://127.0.0.1:8761 \
  -u <rpcuser> -p <rpcpassword> \
  --coinbase-addr=rg1q… -t <threads>
```

`--coinbase-addr` must be an ORU witness v0 address (`rg1q…`) from a wallet you control. Do not use an `rg1p…` RES address as a mining payout. Do not use a Testnet `trg1q…` address. Leave cores free for the node and wallet running on the same machine.

Mainnet activation is 2026-10-25 06:00:00 UTC. On the tested launch build, the first `getblocktemplate` that was not rejected as too far in the future was at 2026-10-25 04:00:01 UTC. That is when a template became available on that clock. It is not a promise that a block will be found then.

Before mining, confirm `getblockchaininfo` reports `"chain": "main"` and block 0 hash `e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468`, and confirm `getpeerinfo` shows at least one peer. One peer is not enough by itself: check that the peer is on this same chain. Do not mine while the peer list is empty.

`-solomining` skips the template checks that refuse `getblocktemplate` with no peers or during initial block download. It does not make a private chain the public chain, and it does not change consensus. Do not set `solomining=1` to silence an error or to obtain a height-1 template. A block mined with no peers can sit on a private branch. That block and its reward can be orphaned when a public chain with more work appears.

There is no designated first miner, no premine, and no founder allocation. The first subsidy-bearing block is height 1. It pays the ordinary 128 ORU subsidy to the miner that finds it, and only after the template rules above allow it.

---

## 13. Connecting a node or wallet

Configuration file location on Windows is `%APPDATA%\RIGOR\rigor.conf` (macOS: `~/Library/Application Support/RIGOR/rigor.conf`; Linux: `~/.rigor/rigor.conf`). It must live in the data directory, not next to the executable.

```text
%APPDATA%\RIGOR\rigor.conf

server=1
rpcuser=YOUR_NAME
rpcpassword=YOUR_PASSWORD
rpcallowip=127.0.0.1
```

Peer discovery uses the compiled DNS seed `seed.rigorchain.com` on **P2P port 8760**. That is not an RPC endpoint, not a mining URL, and it carries no credentials. Optional `addnode=` lines, if you add them, belong on the same P2P port.

Port 8761 is the local JSON-RPC interface and must stay bound to `127.0.0.1`. Do not expose it to the Internet, and do not put an RPC address in any miner configuration other than your own loopback.

After a restart, confirm the node is on mainnet, on this Genesis block, and on the same tip as its peers:

```
rigor-cli getblockchaininfo     # chain must be "main"
rigor-cli getbestblockhash
rigor-cli getblockhash 0        # must be e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468
rigor-cli getblockcount
rigor-cli getpeerinfo
```

A datadir built on any earlier Genesis block is foreign. Wipe it and reinitialize; there is no compatibility branch.

---

## 14. Downloads, integrity check, and where to look

Binaries for the mainnet release are published as GitHub release assets: `rigor-v0.2-windows-x86_64.exe`, `rigor-v0.2-windows-x86_64.zip`, `rigor-v0.2-linux-x86_64.tar.gz`, and the corresponding miner packages, together with `SHA256SUMS.txt` and `MINER-MAINNET-README.txt`. Verify what you downloaded against `SHA256SUMS.txt` from the same release before running it:

```
sha256sum -c SHA256SUMS.txt
```

The node and the wallet can also be built from source (`./autogen.sh && ./configure && make`), which is the only way to check that the binary you run corresponds to the consensus code you read.

- **Website:** https://www.rigorchain.com/
- **GitHub repository — the primary technical reference:** https://github.com/rigorchain/rigorchain
- **Mainnet release:** https://github.com/rigorchain/rigorchain/releases/tag/v0.2-mainnet
- **Latest wallet release:** https://github.com/rigorchain/rigorchain/releases/tag/v0.2.1 (Windows and Linux wallet packages; GUI synchronization-status fix, no consensus change)
- **Explorer (observational, not consensus):** https://explorer.rigorchain.com/

In the repository, the material that matters for the claims above is: `docs/architecture/RIGOR_ARCHITECTURE_PAPER.md` (the design motivation and the architecture in sections 1–2), `docs/whitepaper/RIGOR_PROTOCOL_WHITEPAPER.md` (full protocol narrative), `docs/protocol/RFC-020.md` (issuance specification, the cap derivation, and the schedule in section 7), `docs/protocol/RFC-018.md` (fee destroy), `core/src/consensus/res_issuance.h` and `core/src/consensus/rigor_consensus_lock.h` (the locked constants and their compile-time assertions), `core/src/kernel/chainparams.cpp` (genesis and network identity), `docs/SCRIPT_STRUCTURE.md` (the closed script surface), and `formal_verification/rigor_kernel/` (the Coq model and its inventory).

---

## 15. What this announcement does not claim

It does not claim that the running C++ implementation is equivalent to the verified protocol model. It does not claim the binaries are free of defects. It does not claim that history cannot be rewritten under sufficient hashpower. It does not claim that the RES ceiling will be approached, that a market for either asset will exist, or that 128 ORU per block is a sufficient security budget in external terms. It does not claim that separating monetary roles is better than unifying them; that is the question the architecture explores, not a result it reports. It makes no statement about price, adoption, listings, or returns, and it does not suggest that anyone should mine, hold, or acquire ORU or RES.

The rules are published, the constants are asserted in the code, and every claim above can be recomputed from public data by a full node.
