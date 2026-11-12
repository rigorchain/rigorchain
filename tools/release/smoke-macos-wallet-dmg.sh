#!/usr/bin/env bash
# Mount a just-built official v0.1.3 wallet DMG on GitHub-hosted macOS
# and check bundle + architecture. Build-system only.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=macos_arch.sh
source "${ROOT}/tools/release/macos_arch.sh"
TARGET="${1:?target macos-arm64}"
EXPECT_ARCH="${2:?expected uname arch}"
case "${TARGET}" in
  macos-arm64) ;;
  *) echo "FAIL: macOS packaging target must be macos-arm64, got ${TARGET}" >&2; exit 1 ;;
esac
[[ "${EXPECT_ARCH}" == "arm64" ]] || {
  echo "FAIL: expected architecture must be arm64, got ${EXPECT_ARCH}" >&2
  exit 1
}
VERSION="$(macos_release_version "${ROOT}")" || exit 1
DMG="${ROOT}/dist/release/v${VERSION}/${TARGET}/rigor-v${VERSION}-${TARGET}.dmg"

[[ -f "${DMG}" ]] || { echo "FAIL: missing ${DMG}" >&2; exit 1; }

size="$(stat -f%z "${DMG}" 2>/dev/null || stat -c%s "${DMG}")"
echo "DMG=${DMG}"
echo "SIZE=${size}"
if [[ "${size}" -lt 1000000 ]]; then
  echo "FAIL: DMG is ${size} bytes (identity stub / not a wallet)" >&2
  exit 1
fi

echo "FILE=$(file "${DMG}")"

MNT="$(mktemp -d "${TMPDIR:-/tmp}/rigor-dmg.XXXXXX")"
cleanup() { hdiutil detach "${MNT}" -quiet -force >/dev/null 2>&1 || true; rmdir "${MNT}" 2>/dev/null || true; }
trap cleanup EXIT

hdiutil attach -nobrowse -readonly -mountpoint "${MNT}" "${DMG}"
echo "MOUNTED=${MNT}"
ls -la "${MNT}"

APP=""
for cand in "${MNT}/Rigor-Qt.app" "${MNT}/Rigor Wallet.app" "${MNT}"/*.app; do
  if [[ -d "${cand}" ]]; then
    APP="${cand}"
    break
  fi
done
[[ -n "${APP}" && -d "${APP}" ]] || { echo "FAIL: no .app in DMG" >&2; exit 1; }
echo "APP=${APP}"
python3 "${ROOT}/tools/release/macos_bundle_inspect.py" --bundle "${APP}"

EXE=""
for cand in \
  "${APP}/Contents/MacOS/Rigor-Qt" \
  "${APP}/Contents/MacOS/rigor-qt" \
  "${APP}/Contents/MacOS/Rigor Wallet"
do
  if [[ -x "${cand}" ]]; then
    EXE="${cand}"
    break
  fi
done
[[ -n "${EXE}" ]] || { echo "FAIL: no executable in ${APP}/Contents/MacOS" >&2; ls -la "${APP}/Contents/MacOS" >&2; exit 1; }
echo "EXE=${EXE}"

archs="$(lipo -archs "${EXE}" 2>/dev/null | tr -d '[:space:]' || true)"
echo "LIPO=${archs}"
[[ "${archs}" == "arm64" ]] || {
  echo "FAIL: expected thin arm64, got ${archs}" >&2
  exit 1
}

if [[ -x "${APP}/Contents/MacOS/rigord" ]]; then
  "${APP}/Contents/MacOS/rigord" --version | head -n5
else
  # Node binary is next to the GUI in the official layout when staged;
  # the Qt bundle smoke is the GUI binary --version.
  "${EXE}" --version | head -n8 || true
fi

echo "MACOS_INSTALL_TEST=PASS"
echo "SMOKE_${TARGET}=PASS"
