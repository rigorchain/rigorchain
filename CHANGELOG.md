# Changelog

All notable **product** releases are listed here. Consensus-rule history is
not rewritten in this file; frozen parameters live in
[`docs/MAINNET_PROTOCOL_FREEZE_DECLARATION.md`](docs/MAINNET_PROTOCOL_FREEZE_DECLARATION.md).

```
equivalence_claimed = false
```

## [0.2.3] — 2026-11-12

Tag `v0.2.3` is commit `53879e8ab3b5e9aaeea9c0f26eb5d72378ca23fb`.
Display version in `core/src/clientversion.h` is `v0.2.3`.
Packed `CLIENT_VERSION` remains `25.0.0`.
The published release states that consensus rules, monetary policy, mining
rules, and protocol invariants are unchanged.

Published wallet packages for this tag:

- `rigor-v0.2.3-linux-x86_64.tar.gz`
- `rigor-v0.2.3-windows-x86_64.exe`
- `rigor-v0.2.3-windows-x86_64.zip`
- `rigor-v0.2.3-macos-arm64.dmg`

Changes on this tag, from the commits after `v0.2.2` and the published
release notes:

- 64-bit block and undo file positions for the offset tracked as CVE-2026-117346. Existing undo records stay in the variable-length integer format. 32-bit hosts and FAT32 limits remain.
- Compact-block witness mismatch is treated as a reconstruction failure in `PartiallyDownloadedBlock::FillBlock` (CVE-2025-62859).
- Inbound onion accepts do not receive `-whitelist` (CVE-2025-62860).
- TRUC / BIP 431 sibling eviction is not implemented. Standard relay rejects transaction versions above 2 (CVE-2025-62861).
- Duality Statistics amount baseline, v0.2.2 refresh, and RES capacity display.
- GUI updates stop after the client model is cleared. Seed-watcher callbacks do not run after that model is destroyed.
- Local Tor daemon detection on Windows and Linux.
- DNS seed import accepts a bare domain. An IPv6 documentation-prefix fallback is corrected.
- macOS ARM64 release workflow aligned with this tag.

SHA256 of the four package files, computed locally with `sha256sum`. The digests match the GitHub Release `v0.2.3` record:

```text
4c64b515a522b81765909178e39a4fe8dc5737e39d28e4f208198dfb44aac558  rigor-v0.2.3-linux-x86_64.tar.gz
929fe60ab7abc5208691ee00556fd9fb987e9cea0b6bf48f32ad641704a99810  rigor-v0.2.3-windows-x86_64.exe
4c1c15fd512202788b83ba460c4898566d4fbc608ce5f6e89e4eee265c97fe90  rigor-v0.2.3-windows-x86_64.zip
f51b967c4628cb49440b178c100099f4a0f0c4392563f12786c6c6f670ce533e  rigor-v0.2.3-macos-arm64.dmg
```

## [0.2.2] — 2026-11-05

Tag `v0.2.2`. Wallet release for Linux x86_64 and Windows x86_64.
Notes: [`docs/release/RIGOR_V0.2.2_RELEASE_NOTES.md`](docs/release/RIGOR_V0.2.2_RELEASE_NOTES.md).
That note records a Duality Statistics dashboard, native Tor controls,
Traditional Chinese strings for the Convergence disclaimer, and removal of
automatic port mapping. It records macOS as not packaged for that tag.
Consensus parameters are unchanged in that note.

## [0.2.1] — 2026-10-29

Tag `v0.2.1` exists. Notes:
[`docs/release/RIGOR_V0.2.1_RELEASE_NOTES.md`](docs/release/RIGOR_V0.2.1_RELEASE_NOTES.md).
That note records a GUI synchronization-status fix and states that consensus
rules, the mining algorithm, and monetary parameters are unchanged.
Linux x86_64 and Windows x86_64 packages are named in that note.

## [0.2] — 2026-09-15

Product tag `v0.2`. Not a consensus-parameter change.
Packed `CLIENT_VERSION` remains `25.0.0`.

### Added

- RFC-020 `destroy_cost(h)` as the live RES issuance rule
- Reference SOLO miner on GBT copy-bytes (ORU coinbase; RES is not mined)
- Dual-asset wallet source for ORU and RES

### Changed

- Wallet Convergence copy: destroyed ORU cannot be recovered
- Official wallet packages do not include the miner (miner is a parallel product)

### Security

- Formal verification honesty unchanged: no C++ refinement claim

Build from source: [`docs/developer/Build.md`](docs/developer/Build.md).
Notes: [`docs/release/RIGOR_V0.2_RELEASE_NOTES.md`](docs/release/RIGOR_V0.2_RELEASE_NOTES.md).

## [0.1.3] — 2026-09-03

Historical **stable** channel (`ALPHAPLUS_RELEASE=false`).

- Display identity `v0.1.3` on `rigord`, Qt wallet, Windows installer, and
  macOS `CFBundleShortVersionString`. Packed `CLIENT_VERSION` remains `25.0.0`.
- Wallet packages do not include `rigor-miner`.
- No Consensus Kernel, Transaction Kernel, Ledger, ConnectBlock,
  DisconnectBlock, CoinsDB, wallet SQLite schema, classifier, PoW, ASERT, or
  protocol-parameter changes.

Full notes: [`docs/release/RIGOR_V0.1.3_RELEASE_NOTES.md`](docs/release/RIGOR_V0.1.3_RELEASE_NOTES.md).

## [0.1.2-alphaplus] — 2026-08-23

Historical AlphaPlus channel. Artifact names containing `alphaplus` are
**not** official v0.2 assets.

## [0.1.1-alphaplus] — 2026-08-17

Historical AlphaPlus channel.

## [0.1.0-alpha] — 2026-08-12

First public alpha tag. Companion freeze tag: `v0.1.0-alpha-stable`.

---

Build from source: [`docs/INSTALL.md`](docs/INSTALL.md).
Security reporting: [`SECURITY.md`](SECURITY.md).
