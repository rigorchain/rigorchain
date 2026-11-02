# macOS DMG build template

This is the process that produced the official v0.2.1 Apple Silicon package. It is the template for a later architecture build. The Intel section records why `rigor-v0.2.1-macos-x86_64.dmg` was not produced from this source pin.

Three results are separate:

| Result | What was done for v0.2.1 ARM64 |
|---|---|
| Build success | GitHub Actions compiled and packaged the DMG. Run `41424936312`, workflow commit `32482688f8c12fa3a6f22bb0b71200df778e9bb1`. |
| Package integrity | The DMG is a non-empty UDZO image with a `koly` trailer. Its SHA256 was calculated locally and again after download from the GitHub Release. |
| Architecture and dependency inspection | On the runner, 24 Mach-O files were thin `arm64`, minimum OS was at most 15.0.0, and load commands did not point at Homebrew. The job still exited 1 because the checker treated each Qt plugin's own install name as a dependency. |
| Physical Mac runtime testing | Not performed. No Mac launched the app. |

## Source identity

| Field | Value |
|---|---|
| Repository | `rigorchain/rigorchain` (public) |
| Wallet source commit | `2e5b1fb08ba9a09434e67b1ce7724d174485b452` |
| Parent | `c1368f6870dcbf92b30e959c813fb4a260dcd089` |
| Dependency manifest | `tools/release/macos-v0.2.1/ARM64_DEPENDENCY_LOCK.md` |
| Manifest SHA256 | `991163c6156d9e511eb37e5a8bdc768cfb7d482af17545b36e5d132b3cb81f9d` |

The release branch that once pointed at this commit was deleted. Checkout uses the commit SHA. `actions/checkout` uses `fetch-depth: 2` so the parent check can run. The job rejects any other repository, any other SHA, any other parent, and any diff other than the manifest.

Do not merge that commit into `main`. The workflow file lives on `main`. The wallet tree is checked out by SHA inside the job.

## Workflow and runner

Workflow: `.github/workflows/build-macos-v021-arm64.yml`

| Item | ARM64 value used |
|---|---|
| Event | `workflow_dispatch` input `source_sha` |
| Runner | `macos-15` exactly |
| Machine | 3-core M1, 7 GB RAM, 14 GB SSD, architecture `arm64` |
| Permissions | `contents: read`, `actions: write` |
| Checkout action | `actions/checkout@d23441a48e516b6c34aea4fa41551a30e30af803` |
| Upload action | `actions/upload-artifact@b7c566a772e6b6bfb58ed0dc250532a479d7789f` |
| Artifact name | `rigor-v0.2.1-macos-arm64` |
| Artifact retention | 14 days |
| Build script | `tools/release/macos-v0.2.1/build-macos-wallet-dmg.sh` |
| Output | `dist/release/v0.2.1/macos-arm64/rigor-v0.2.1-macos-arm64.dmg` |

The runner gate requires `uname -m` = `arm64`, `sw_vers` product version matching `^15([.]|$)`, `xcodebuild`, Python >= 3.10, and at least 8 GiB free disk. `MACOSX_DEPLOYMENT_TARGET` is `15.0`.

`macos-latest` is not this runner. On the date of the build it maps to macOS 26 ARM64. Larger labels such as `macos-15-large` are a different, billed class and were not used.

## Actions cost

Checked on 2026-11-02.

- The repository is public (`private: false`).
- GitHub's published runner table lists `macos-15` and `macos-15-intel` as standard hosted runners. Standard runner time on a public repository is free and does not draw the private-repository included compute amount.
- The private-repository rate for this macOS class is published as `actions_macos` at $0.062 per minute. That rate applies to private repositories and to the included-compute ledger. It was not the charge path for this public job.
- Larger runners are billed on public repositories. This build did not use one.
- `GET /user/settings/billing/actions` with the authenticated CLI returned HTTP 404, so the account's current minute counter and plan storage balance were not readable. Billing settings and payment methods were not changed.
- Artifact storage is a separate quota from runner minutes. GitHub Free includes 500 MB, shared with GitHub Packages. The uploaded ARM64 artifact archive is 19903815 bytes. Retention on the workflow artifact is 14 days. The Release asset is release storage, not that Actions artifact.

## Dependencies

Homebrew is forced off the live formula API:

```text
HOMEBREW_NO_AUTO_UPDATE=1
HOMEBREW_NO_INSTALL_UPGRADE=1
HOMEBREW_NO_BOTTLE_SOURCE_FALLBACK=1
HOMEBREW_NO_INSTALL_FROM_API=1
```

`homebrew/core` is cloned and checked out at `b665a4681f55b9f228a23c01b1646dcd882ff338`. Before installation, `brew info --json=v2` for `jpeg-turbo`, `boost`, and `qt@5` must match the pinned version and bottle SHA256. The live API had already moved `jpeg-turbo` to 3.2.0; that index is rejected.

Each bottle blob is downloaded from `ghcr.io` and hashed before `brew install <formula>`. The compressed bottle is not passed to `brew install`. A loose `.rb` file outside the tap is not used. `--ignore-dependencies` is not required for the successful install path; the job installs formulas from this tap in dependency order.

Primary libraries, all `arm64_sequoia` unless noted:

| Formula | Version installed | Bottle SHA256 |
|---|---|---|
| `qt@5` | 5.15.19 | `f9981c2cffae3a70a3e9c09e2b581c74566a2dd9294c6dee6e01ff6facf7935f` |
| `boost` | 1.92.0_1 | `bc88ce997d845e2aac0a1cebc6c89447ae4c31489064f000744fa5c3683c48a6` |
| `libevent` | 2.1.13_1 | `55344e687f9be992ae9dca23aad5bf6f3bbaa9e63031915cd2738da11787500b` |
| `qrencode` | 4.1.1_1 | `8f2cb82a0924e1d5b2ce0f5edbc59884bfb35b5ab8a7dae5a7b3f362679e278a` |
| `jpeg-turbo` | 3.2.1 | `802413c7772789ae8c9d0444042e9a766bcff1beae3919ea5716022aa9dafce7` |

The closure also pins `m4` 1.4.21, `autoconf` 2.73, `automake` 1.19, `libtool` 2.6.2, `pkgconf` 3.0.10, `ca-certificates` 2026-10-20 (`all`), `openssl@4` 4.1.0, `xz` 5.8.4, `lz4` 1.10.0, `zstd` 1.5.7_1, `libpng` 1.6.59, `brotli` 1.2.1, `freetype` 2.15.0, `libunistring` 1.5, `json-c` 0.19, `gettext` 1.0, `pcre2` 10.49, `glib` 2.90.1, `giflib` 6.1.3, `webp` 1.6.0, `libtiff` 4.7.2, `md4c` 0.6.0, `readline` 8.3.6, `sqlite` 3.54.1, and `icu4c@79` 79.1. Their blob SHA256 values are in the workflow.

libsodium 1.0.22 is built from source by `tools/release/fetch-libsodium.sh`. Archive SHA256: `adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349`. Configure uses `--enable-static-libsodium`. The fetch script does not run minisign.

DMG layout wheels, installed with `pip install --no-index --no-deps` into `RIGOR_MACOS_DMG_VENV`:

| Wheel | SHA256 |
|---|---|
| `ds_store` 1.3.3 | `b92a371efbf1b4ccce2a04d1ed13fceacc4736c81ba09cf5aefb74c088160a35` |
| `mac_alias` 2.2.3 | `7362b521d2132ef92f606a37abfed5fcd849ceb2f28b6f9743e014b02af92f0d` |

## Build, bundle, signing, and DMG

The build script selects `macos-arm64` when `uname -m` is `arm64`, and `macos-x86_64` when it is `x86_64`. Configure is:

```text
./configure --enable-static-libsodium --without-bdb --with-sqlite=yes --with-gui=qt5 \
  --disable-bench --disable-tests --disable-gui-tests --disable-fuzz-binary --disable-man \
  CXXFLAGS="-O2 -DRIGOR_PRODUCTION_MAINNET"
```

`make` is followed by `make deploy`. The v0.2.1 overlay copies `Info.plist.in`, `macdeployqtplus`, and `dylib_resolve.py` over the legacy macOS packaging files for that workspace only.

`macdeployqtplus` signs with `codesign --deep --force --sign -`. That is ad-hoc signing. There is no Developer ID, no notarization, and no Gatekeeper bypass.

The same script creates a read-write image and then compresses it:

```text
hdiutil create <temp> -srcfolder dist -format UDRW -volname <app>
hdiutil convert <temp> -format UDZO -o <app> -imagekey zlib-level=9
```

The app bundle in the image is `Rigor-Qt.app`. `Info.plist` from the v0.2.1 template sets:

- `CFBundleIdentifier` = `org.rigor.RigorQt`
- `CFBundleName` = `Rigor Wallet`
- `CFBundleExecutable` = `rigor-qt`
- `CFBundleShortVersionString` and `CFBundleVersion` = `0.2.1`
- `LSMinimumSystemVersion` = `15.0.0`
- `LSArchitecturePriority` = `x86_64` only

The last key is the template text. It is not the Mach-O architecture. The ARM64 binaries were still thin `arm64`.

## Inspection that must be copied carefully

After `hdiutil attach -readonly`, check the plist identity, version, and `15.0.0`, then `codesign -dv`. The successful run recorded `Signature=adhoc` and `Identifier=org.rigor.RigorQt`.

For every Mach-O file, `lipo -info` must be a non-fat file of the job architecture. On the ARM64 job that is `arm64`, and the string `x86_64` fails the file. `otool -l` must show `minos` or `LC_VERSION_MIN_MACOSX` version `<= 15.0.0`.

`otool -L` prints the file path, then the binary's own install name (`LC_ID_DYLIB`), then load commands. Only the load commands are runtime dependencies. Skip the first two lines. Fail if a later line contains `/opt/homebrew` or `/usr/local/opt`.

### Failure on the ARM64 run

Run `41424936312` built and uploaded the DMG, then failed in `Inspect the application bundle`. The only failures were:

- `Contents/PlugIns/platforms/libqminimal.dylib` install name `/opt/homebrew/opt/qt@5/plugins/platforms/libqminimal.dylib`
- `Contents/PlugIns/platforms/libqcocoa.dylib` install name `/opt/homebrew/opt/qt@5/plugins/platforms/libqcocoa.dylib`
- `Contents/PlugIns/styles/libqmacstyle.dylib` install name `/opt/homebrew/opt/qt@5/plugins/styles/libqmacstyle.dylib`

Those lines are the plugins' own install names, not libraries the plugins load. No architecture or minimum-OS failure was printed (`MACHO_COUNT 24`). Workflow commit `472d6abd87eddbecc15813d04803855fec4b897d` skips the install-name line. That commit was not rebuilt. Do not classify `LC_ID_DYLIB` as an external Homebrew dependency again.

## Integrity and checksums

On the runner the build step requires a non-empty DMG and prints `shasum -a 256`.

Local checks of the downloaded file:

| Check | Result |
|---|---|
| DMG size | 19946232 bytes |
| DMG SHA256 | `4394422bd6733163d93f0a171199cb9bbb6c6dc5d0367b843b21ed021c027fbb` |
| Actions artifact archive SHA256 | `f99e96723120520bbb418b98c49fd2719b8f90276fb53a99566f7d242988ccec` |
| Archive size | 19903815 bytes |

The artifact digest is the zip GitHub stored. It is not the DMG hash. The zip contains one member, `rigor-v0.2.1-macos-arm64.dmg`. The DMG starts with a zlib stream and ends with the `koly` trailer. Decompressing the Apple APFS partition shows `Rigor-Qt.app` and the plist values above.

## Release upload

Upload the DMG bytes to the existing tag. Do not upload the Actions zip. Do not create a tag or a release.

For v0.2.1 the asset is:

`https://github.com/rigorchain/rigorchain/releases/download/v0.2.1/rigor-v0.2.1-macos-arm64.dmg`

`SHA256SUMS.txt` is replaced only after the new file is the previous bytes plus one new line. The preserved lines are:

```text
2bc70b3a79d1f7eb1ad6de09023b0586a9058ebcaab60bd1a8004d6173c706a2  rigor-v0.2.1-windows-x86_64.exe
b5690ab34d079478d245fbae41874ecb0f18cefe78dfa52b09f945b7bd998b73  rigor-v0.2.1-windows-x86_64.zip
90e8e403ea8067723f0215405366c822705fe1213274a1ef57afde1e3f4bd11e  rigor-v0.2.1-linux-x86_64.tar.gz
4394422bd6733163d93f0a171199cb9bbb6c6dc5d0367b843b21ed021c027fbb  rigor-v0.2.1-macos-arm64.dmg
```

Windows and Linux asset sizes and digests were unchanged by that upload. Release notes name Windows x86_64, Linux x86_64, and macOS ARM64. The macOS note says the package is ad-hoc signed, not notarized, Apple Silicon only, and requires macOS 15.0 or later.

## Architecture differences

| Item | ARM64, executed | x86_64, not executed |
|---|---|---|
| Runner label | `macos-15` | `macos-15-intel` is the standard Intel label. It is free on a public repository. It was not started. |
| Hardware class | 3-core M1, 7 GB RAM | 4-core Intel, 14 GB RAM, 14 GB SSD |
| `uname -m` gate | `arm64` | must be `x86_64` |
| Bottle tag | `arm64_sequoia` | `sequoia` |
| Homebrew prefix | `/opt/homebrew` | `/usr/local` |
| `lipo` requirement | thin `arm64`, no `x86_64` | thin `x86_64`, no `arm64` |
| Package name | `rigor-v0.2.1-macos-arm64.dmg` | `rigor-v0.2.1-macos-x86_64.dmg` |
| Minimum OS | 15.0.0 | same plist value; do not lower it |

## Intel stop

`macos-15-intel` is available as a standard public runner, and the build script already selects `macos-x86_64` from `uname -m`. The pinned Homebrew commit does not provide macOS 15 Intel bottles for the libraries this package links.

At `b665a4681f55b9f228a23c01b1646dcd882ff338`:

- `qt@5` 5.15.19 has `arm64_sequoia` and Intel `sonoma` (macOS 14). It has no `sequoia` bottle.
- `boost` 1.92.0 revision 1 has no Intel macOS bottle.
- `jpeg-turbo` 3.2.1 has no Intel macOS bottle. Its formula builds Intel SIMD with `nasm` only when compiling from source.
- `automake` 1.19 has no `sequoia` bottle.

`HOMEBREW_NO_BOTTLE_SOURCE_FALLBACK=1` would refuse a source build. Using the macOS 14 Intel bottle, or compiling Qt and jpeg-turbo on the runner, is a different dependency set from the verified ARM64 lock. No Intel workflow was added, and no Intel DMG was built. The v0.2.1 Release was not changed by this Intel check.
