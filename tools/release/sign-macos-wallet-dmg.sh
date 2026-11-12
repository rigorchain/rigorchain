#!/usr/bin/env bash
# P0-095: Developer ID Application signing for official macOS wallet DMGs.
# Notarization is a later TASK (not P0-096). Does not modify consensus, wallet schema, or Guardian.
#
# Required GitHub secrets (all three) to SIGN:
#   MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE  base64-encoded .p12
#   MACOS_DEVELOPER_ID_APPLICATION_PASSWORD     PKCS#12 password
#   MACOS_DEVELOPER_ID_APPLICATION_IDENTITY     "Developer ID Application: … (TEAMID)"
# Identity may be omitted if the p12 contains exactly one Developer ID Application cert.
#
# Missing secrets or non-Darwin host → DEFERRED, exit 0.
# RELEASE_BLOCKED_BY_MACOS_SIGNING is always FALSE from this script.
set +x
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=macos_arch.sh
source "${ROOT}/tools/release/macos_arch.sh"
VERSION="$(macos_release_version "${ROOT}")" || exit 1
ENTITLEMENTS="${ROOT}/tools/release/macos-developer-id.entitlements"

PROBE=0
TARGET=""
if [[ "${1:-}" == "--probe" ]]; then
  PROBE=1
  TARGET="${2:-}"
elif [[ -n "${1:-}" ]]; then
  TARGET="$1"
fi

CERT="${MACOS_DEVELOPER_ID_APPLICATION_CERTIFICATE:-}"
PASS="${MACOS_DEVELOPER_ID_APPLICATION_PASSWORD:-}"
IDENTITY="${MACOS_DEVELOPER_ID_APPLICATION_IDENTITY:-}"

emit() {
  echo "$1"
}

defer() {
  local reason="$1"
  emit "MACOS_SIGNING_STATUS=DEFERRED"
  emit "MACOS_SIGNING_DEFERRED_REASON=${reason}"
  emit "MACOS_SIGNING_VERIFICATION=SKIPPED"
  emit "MACOS_SPCTL_ASSESSMENT=SKIPPED"
  emit "MACOS_NOTARIZATION_READY=FALSE"
  emit "RELEASE_BLOCKED_BY_MACOS_SIGNING=FALSE"
  emit "CONSENSUS_CODE_CHANGED=FALSE"
  emit "DATABASE_FORMAT_CHANGED=FALSE"
  emit "FINAL_VERDICT=PASS"
  exit 0
}

fail_signed_attempt() {
  emit "MACOS_SIGNING_STATUS=FAIL"
  emit "MACOS_SIGNING_VERIFICATION=FAIL"
  emit "MACOS_NOTARIZATION_READY=FALSE"
  emit "RELEASE_BLOCKED_BY_MACOS_SIGNING=FALSE"
  emit "CONSENSUS_CODE_CHANGED=FALSE"
  emit "DATABASE_FORMAT_CHANGED=FALSE"
  emit "FINAL_VERDICT=FAIL"
  echo "FAIL: $1" >&2
  exit 1
}

case "${TARGET}" in
  ""|macos-arm64) ;;
  *)
    echo "FAIL: macOS packaging target must be macos-arm64, got ${TARGET}" >&2
    exit 1
    ;;
esac

secrets_present=1
[[ -n "${CERT}" ]] || secrets_present=0
[[ -n "${PASS}" ]] || secrets_present=0

if [[ "${secrets_present}" -eq 0 ]]; then
  defer "github_secrets_unavailable"
fi
if [[ "$(uname -s)" != "Darwin" ]]; then
  defer "host_not_darwin"
fi
if [[ "${PROBE}" -eq 1 ]]; then
  emit "MACOS_SIGNING_STATUS=SECRETS_PRESENT"
  emit "MACOS_SIGNING_VERIFICATION=SKIPPED"
  emit "MACOS_SPCTL_ASSESSMENT=SKIPPED"
  emit "MACOS_NOTARIZATION_READY=FALSE"
  emit "RELEASE_BLOCKED_BY_MACOS_SIGNING=FALSE"
  emit "CONSENSUS_CODE_CHANGED=FALSE"
  emit "DATABASE_FORMAT_CHANGED=FALSE"
  emit "FINAL_VERDICT=PASS"
  exit 0
fi

[[ "${TARGET}" == "macos-arm64" ]] || fail_signed_attempt "macOS packaging target must be macos-arm64"

DMG="${ROOT}/dist/release/v${VERSION}/${TARGET}/rigor-v${VERSION}-${TARGET}.dmg"
[[ -f "${DMG}" ]] || fail_signed_attempt "missing ${DMG}"
[[ -f "${ENTITLEMENTS}" ]] || fail_signed_attempt "missing entitlements ${ENTITLEMENTS}"

WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/rigor-p095.XXXXXX")"
P12="${WORKDIR}/developer-id.p12"
KEYCHAIN="${WORKDIR}/rigor-p095.keychain-db"
MNT="${WORKDIR}/mnt"
KC_PASS="$(python3 -c 'import secrets; print(secrets.token_urlsafe(24))')"

cleanup() {
  if [[ -d "${MNT}" ]]; then
    hdiutil detach "${MNT}" -quiet -force >/dev/null 2>&1 || true
  fi
  if [[ -f "${KEYCHAIN}" ]]; then
    security delete-keychain "${KEYCHAIN}" >/dev/null 2>&1 || true
  fi
  rm -rf "${WORKDIR}" >/dev/null 2>&1 || true
}
trap cleanup EXIT

python3 - "${CERT}" "${P12}" <<'PY'
import base64, sys
from pathlib import Path
blob = sys.argv[1].strip()
Path(sys.argv[2]).write_bytes(base64.b64decode(blob))
PY
[[ -s "${P12}" ]] || fail_signed_attempt "certificate secret is not valid base64 PKCS#12"

security create-keychain -p "${KC_PASS}" "${KEYCHAIN}" >/dev/null
security set-keychain-settings -lut 21600 "${KEYCHAIN}"
security unlock-keychain -p "${KC_PASS}" "${KEYCHAIN}"
security list-keychains -d user -s "${KEYCHAIN}" $(security list-keychains -d user | tr -d '"')
if ! security import "${P12}" -k "${KEYCHAIN}" -P "${PASS}" -T /usr/bin/codesign -T /usr/bin/security >/dev/null; then
  fail_signed_attempt "security import of Developer ID PKCS#12 failed"
fi
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "${KC_PASS}" "${KEYCHAIN}" >/dev/null

if [[ -z "${IDENTITY}" ]]; then
  IDENTITY="$(security find-identity -v -p codesigning "${KEYCHAIN}" \
    | grep -F 'Developer ID Application' \
    | head -n1 \
    | sed -E 's/.*"([^"]+)".*/\1/' || true)"
fi
[[ -n "${IDENTITY}" ]] || fail_signed_attempt "no Developer ID Application identity in p12 or secret"
echo "${IDENTITY}" | grep -q 'Developer ID Application' || \
  fail_signed_attempt "identity is not Developer ID Application"

emit "MACOS_SIGNING_IDENTITY_KIND=Developer ID Application"

hdiutil convert "${DMG}" -format UDRW -ov -o "${WORKDIR}/rw" >/dev/null
RW_DMG="${WORKDIR}/rw.dmg"
[[ -f "${RW_DMG}" ]] || fail_signed_attempt "hdiutil convert did not produce ${RW_DMG}"
mkdir -p "${MNT}"
hdiutil attach -nobrowse -readwrite -owners off -mountpoint "${MNT}" "${RW_DMG}" >/dev/null

APP=""
for cand in "${MNT}/Rigor-Qt.app" "${MNT}/Rigor Wallet.app" "${MNT}"/*.app; do
  if [[ -d "${cand}" ]]; then
    APP="${cand}"
    break
  fi
done
[[ -n "${APP}" && -d "${APP}" ]] || fail_signed_attempt "no .app in DMG"

sign_macho() {
  local path="$1"
  local extra=()
  if [[ "${path}" == *.app ]]; then
    extra+=(--entitlements "${ENTITLEMENTS}")
  fi
  codesign --force --options runtime --timestamp --sign "${IDENTITY}" "${extra[@]}" "${path}"
}

TMP_LIST="${WORKDIR}/macho.txt"
: > "${TMP_LIST}"
while IFS= read -r -d '' f; do
  if file -b "${f}" 2>/dev/null | grep -q 'Mach-O'; then
    printf '%s\n' "${f}" >> "${TMP_LIST}"
  fi
done < <(find "${APP}" -type f -print0)
# Inside-out: longest path first (nested frameworks before the outer bundle).
while IFS= read -r f; do
  [[ -n "${f}" ]] || continue
  codesign --force --options runtime --timestamp --sign "${IDENTITY}" "${f}"
done < <(awk '{ print length, $0 }' "${TMP_LIST}" | sort -nr | cut -d' ' -f2-)
sign_macho "${APP}"

if ! codesign --verify --strict --deep --verbose=2 "${APP}"; then
  fail_signed_attempt "codesign --verify failed for ${APP}"
fi
VERIFY_APP=PASS

RUNTIME_OK=0
if codesign --display --verbose=2 "${APP}" 2>&1 | grep -Eq 'flags=.*runtime'; then
  RUNTIME_OK=1
fi
TIMESTAMP_OK=0
if codesign --display --verbose=2 "${APP}" 2>&1 | grep -Eq 'Timestamp=|Authority=Developer ID'; then
  TIMESTAMP_OK=1
fi

SPCTL_EXECUTE_OUT="$(spctl --assess --type execute --verbose "${APP}" 2>&1 || true)"
SPCTL_EXECUTE=REJECTED
if echo "${SPCTL_EXECUTE_OUT}" | grep -qi accepted; then
  SPCTL_EXECUTE=ACCEPTED
elif echo "${SPCTL_EXECUTE_OUT}" | grep -qi 'Unnotarized Developer ID'; then
  SPCTL_EXECUTE=UNNOTARIZED_DEVELOPER_ID
fi
emit "MACOS_SPCTL_EXECUTE=${SPCTL_EXECUTE}"

hdiutil detach "${MNT}" -quiet -force >/dev/null
rmdir "${MNT}" 2>/dev/null || true
MNT=""

hdiutil convert "${RW_DMG}" -format UDZO -imagekey zlib-level=9 -ov -o "${WORKDIR}/signed" >/dev/null
SIGNED_DMG="${WORKDIR}/signed.dmg"
[[ -f "${SIGNED_DMG}" ]] || fail_signed_attempt "hdiutil convert did not produce ${SIGNED_DMG}"
codesign --force --timestamp --sign "${IDENTITY}" "${SIGNED_DMG}"
if ! codesign --verify --verbose=2 "${SIGNED_DMG}"; then
  fail_signed_attempt "codesign --verify failed for DMG"
fi
VERIFY_DMG=PASS

SPCTL_INSTALL_OUT="$(spctl --assess --type install --verbose "${SIGNED_DMG}" 2>&1 || true)"
SPCTL_INSTALL=REJECTED
if echo "${SPCTL_INSTALL_OUT}" | grep -qi accepted; then
  SPCTL_INSTALL=ACCEPTED
elif echo "${SPCTL_INSTALL_OUT}" | grep -qi 'Unnotarized Developer ID'; then
  SPCTL_INSTALL=UNNOTARIZED_DEVELOPER_ID
fi
emit "MACOS_SPCTL_INSTALL=${SPCTL_INSTALL}"

cp -a "${SIGNED_DMG}" "${DMG}"

NOTARY_READY=FALSE
if [[ "${VERIFY_APP}" == PASS && "${VERIFY_DMG}" == PASS && "${RUNTIME_OK}" -eq 1 && "${TIMESTAMP_OK}" -eq 1 ]]; then
  NOTARY_READY=TRUE
fi

emit "MACOS_SIGNING_STATUS=SIGNED"
emit "MACOS_SIGNING_VERIFICATION=PASS"
emit "MACOS_SPCTL_ASSESSMENT=${SPCTL_EXECUTE}"
emit "MACOS_NOTARIZATION_READY=${NOTARY_READY}"
emit "RELEASE_BLOCKED_BY_MACOS_SIGNING=FALSE"
emit "CONSENSUS_CODE_CHANGED=FALSE"
emit "DATABASE_FORMAT_CHANGED=FALSE"
emit "FINAL_VERDICT=PASS"
emit "SIGNED_DMG=${DMG}"
