# RIGOR Wallet v0.2.2

**Classification:** PUBLIC release notes for tag `v0.2.2`.

Packed `CLIENT_VERSION` remains `25.0.0`. The display version is `RIGOR_RELEASE_VERSION` (`v0.2.2`). This release does not change consensus, issuance, or chain parameters.

## Overview

RIGOR Wallet v0.2.2 is a mainnet full-node wallet release for Linux x86_64 and Windows x86_64. It adds a local Duality Statistics dashboard, native Tor controls, Traditional Chinese localization for the Convergence disclaimer and the new navigation entry, and it removes automatic port mapping.

macOS ARM64 is not part of this release. No macOS download is published.

The packages are unsigned. Apple Developer ID signing, Apple notarization, and Microsoft Authenticode are not release prerequisites.

## Major changes

- Duality Statistics reads this node's local economics index. It does not call an external API.
- Native Tor modes can be selected in the network settings. The default launch does not enable a SOCKS proxy.
- Simplified Chinese and Traditional Chinese navigation names are distinct.
- Automatic UPnP and NAT-PMP are disabled. Inbound connectivity requires a manual router forward.

## Duality Statistics

The page title is `Duality Statistics` in every wallet language. Only the navigation entry is translated:

- English: `Duality Statistics`
- Simplified Chinese: `双资产统计`
- Traditional Chinese: `雙資產統計`

The page shows four ORU figures and two RES figures, plus an Index & Data Status panel. Unavailable values stay `N/A`. A failed refresh keeps the last valid figures and can show `Stale Data`. The page refreshes from the local index on the existing 300-second interval.

## Native Tor support

Settings offer Normal network, Tor outbound, Tor inbound and outbound, dual clearnet and Tor, and Tor only. Normal network is the default. Choosing a Tor mode is what writes a SOCKS proxy, normally `127.0.0.1:9050`. A new install does not do that by itself, so the wallet can still reach clearnet peers.

Tor control prefers hashed-password authentication when a password is configured, then SAFECOOKIE, and uses NULL only when it is the only offered method. Onion-service private keys are redacted in failure logs. The mainnet onion listener target remains `127.0.0.1:8762`. RPC remains loopback-only by default (`127.0.0.1` and `::1`, port `8761`).

## Localization improvements

The Convergence disclaimer follows the wallet language catalogs instead of the operating-system language. Simplified Chinese and Traditional Chinese each keep their own wording. Other languages keep the English source until a catalog translates it.

## Automatic port mapping deprecation

`-upnp` and `-natpmp` are ignored. The node does not discover or create UPnP, NAT-PMP, or PCP mappings. Users who need inbound connections must forward the public P2P port on the router themselves. Mainnet P2P is port `8760`.

## Linux x86_64 download

`rigor-v0.2.2-linux-x86_64.tar.gz`

Extract the archive and start `./bin/rigor-wallet`. The launcher stores data in a `data` directory next to the `bin` directory and allows RPC only from `127.0.0.1`. Qt is supplied by the system. The package does not contain a personal wallet or a chain database.

## Windows x86_64 download

`rigor-v0.2.2-windows-x86_64.exe`

`rigor-v0.2.2-windows-x86_64.zip`

Run the installer, or extract the zip and start `rigor-wallet.exe`. Do not start the program inside `internal\`. Data is stored in a `data` folder next to `rigor-wallet.exe`. Qt is linked into the wallet executable. The package does not contain a personal wallet or a chain database.

Windows SmartScreen can warn because the executable is unsigned. That warning is a platform message. It is not a failed build.

## Installation

1. Download the archive for the system you are using.
2. Compare its SHA256 checksum with `SHA256SUMS` before opening it.
3. Install or extract it into an empty directory.
4. Start `rigor-wallet` on Linux or `rigor-wallet.exe` on Windows.
5. Let the full node sync mainnet. Duality Statistics stays incomplete until the local index has data.

## SHA256 verification

Linux:

```text
sha256sum -c SHA256SUMS
```

Windows PowerShell:

```text
Get-FileHash .\rigor-v0.2.2-windows-x86_64.exe -Algorithm SHA256
```

Compare the printed digest with the matching line in `SHA256SUMS`.

## Platform support and limitations

| Platform | This release |
|---|---|
| Linux x86_64 | Packaged. GUI startup was checked on the build host. |
| Windows x86_64 | Packaged. The dashboard layout was visually accepted on native Windows. Automated Windows regression was not run in this environment. |
| macOS ARM64 | Deferred. Not packaged and not claimed as tested for v0.2.2. |

## Upgrade notes

This build is a mainnet full node. It does not enable mining. It does not enable a proxy unless a saved configuration or an explicit setting already does. Existing wallets and chain data in the data directory are kept. Automatic port mapping from an older configuration is not re-enabled.
