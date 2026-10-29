# RIGOR Wallet v0.2.1 Release Notes

| Field | Value |
|-------|-------|
| **Version** | `0.2.1` |
| **Tag** | `v0.2.1` |
| **Title** | RIGOR Wallet v0.2.1 |
| **Date** | 2026-10-29 |
| **Channel** | `stable` |
| **Nature** | Wallet GUI patch release. **Not** a consensus-parameter change. |

```
refinement_proved=false
LEVEL_5_CLAIMED=false
CPP_VERIFIED=false
```

Packed `CLIENT_VERSION` remains `25.0.0`. Product tag is **v0.2.1**.

Official names follow [v0.2-mainnet](https://github.com/rigorchain/rigorchain/releases/tag/v0.2-mainnet).

---

## What changed

RIGOR Wallet v0.2.1 fixes a GUI synchronization-status issue that could leave the progress overlay displaying a stale block height after rapid block synchronization. No consensus rules, mining algorithm, or monetary parameters have changed.

Detail: block-tip notifications are rate limited so that a fast chain cannot repaint the status bar hundreds of times per second. If a burst of blocks ended inside one rate-limit window, the final update was discarded and the overlay kept showing the last painted height until another block arrived. The wallet now retains the newest suppressed update and delivers it after the burst, so the status bar and the overlay converge on the node's actual tip without waiting for another block and without restarting the wallet.

The rate limiting itself is unchanged, reindex behaviour is unchanged, and synchronization completion is never reported unless the chain tip is genuinely current.

---

## Supported platforms

| Platform | Arch | Artifact |
|----------|------|----------|
| Windows | x86_64 | `rigor-v0.2.1-windows-x86_64.exe` and `.zip` |
| Linux | x86_64 | `rigor-v0.2.1-linux-x86_64.tar.gz` |

---

## Downloads

Release page: https://github.com/rigorchain/rigorchain/releases/tag/v0.2.1

| Artifact | Link |
|----------|------|
| Windows installer | https://github.com/rigorchain/rigorchain/releases/download/v0.2.1/rigor-v0.2.1-windows-x86_64.exe |
| Windows zip | https://github.com/rigorchain/rigorchain/releases/download/v0.2.1/rigor-v0.2.1-windows-x86_64.zip |
| Linux tarball | https://github.com/rigorchain/rigorchain/releases/download/v0.2.1/rigor-v0.2.1-linux-x86_64.tar.gz |
| Checksums | https://github.com/rigorchain/rigorchain/releases/download/v0.2.1/SHA256SUMS.txt |

### SHA-256

```text
2bc70b3a79d1f7eb1ad6de09023b0586a9058ebcaab60bd1a8004d6173c706a2  rigor-v0.2.1-windows-x86_64.exe
b5690ab34d079478d245fbae41874ecb0f18cefe78dfa52b09f945b7bd998b73  rigor-v0.2.1-windows-x86_64.zip
90e8e403ea8067723f0215405366c822705fe1213274a1ef57afde1e3f4bd11e  rigor-v0.2.1-linux-x86_64.tar.gz
```

Verify before running:

```bash
sha256sum -c SHA256SUMS.txt
```

---

## Installation

### Windows wallet

1. Download `rigor-v0.2.1-windows-x86_64.exe` **or** the zip and extract it.
2. Run the installer, or use `rigord.exe` from the zip.
3. Always start `rigor-wallet.exe`. Do not open files inside `internal\`.
4. Datadir: the `data` folder next to `rigor-wallet.exe`. Config: `<install>\data\rigor.conf`.

### Linux wallet

```bash
tar -xzf rigor-v0.2.1-linux-x86_64.tar.gz
./bin/rigord --version
./bin/rigor-wallet
```

Datadir: `data` inside the extracted tree. Do not run `bin/internal/*` directly.

### Upgrading from v0.2

Quit the wallet completely, including the tray icon, then install v0.2.1 over the existing installation. Wallet files and the data directory are retained. No reindex and no resynchronization are required.

---

## Release contents

This release contains the official Linux and Windows wallet packages and their checksums. Mining software is not part of this release. The v0.2 release and its assets are unchanged.

---

## Known limitations

These are packaging and signing facts. They do **not** mark v0.2.1 as a test or experimental channel.

- Not a Guix-reproducible signed/notarized build.
- Windows installers are unsigned.
- macOS packages are not part of this release.
- Formal verification of the running C++ binary is **not** claimed.

## What this is not

- Not a consensus-parameter change
- Not a change to PoW/OpuHash, block format, or the P2P protocol
- Not a change to ORU/RES issuance or monetary parameters
- Not CPP verified
- Not Level 5
