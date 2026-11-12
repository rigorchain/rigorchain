#!/usr/bin/env bash
# Selection-logic tests. They do not compile, sign, or package a DMG.
# A passing run is not native Apple Silicon acceptance.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=macos_arch.sh
source "${ROOT}/tools/release/macos_arch.sh"

fail=0
check() {
  local name="$1"
  local got="$2"
  local want="$3"
  if [[ "${got}" == "${want}" ]]; then
    echo "PASS ${name}"
  else
    echo "FAIL ${name}" >&2
    echo "  got:  ${got}" >&2
    echo "  want: ${want}" >&2
    fail=1
  fi
}

expect_fail() {
  local name="$1"
  shift
  local err
  if err="$("$@" 2>&1)"; then
    echo "FAIL ${name} (expected rejection, got: ${err})" >&2
    fail=1
  else
    echo "PASS ${name}"
  fi
}

check "arm64 host defaults to arm64" \
  "$(macos_resolve_arch arm64 1 "" 0 "")" \
  "host_hardware=arm64 process=arm64 requested=arm64 target=macos-arm64 mach=arm64 reexec=0 native_arm64=1 cross=0"

check "explicit arm64 on arm64 host" \
  "$(macos_resolve_arch arm64 1 macos-arm64 0 "")" \
  "host_hardware=arm64 process=arm64 requested=arm64 target=macos-arm64 mach=arm64 reexec=0 native_arm64=1 cross=0"

check "aarch64 spelling defaults to arm64" \
  "$(macos_resolve_arch aarch64 unknown "" 0 "")" \
  "host_hardware=arm64 process=arm64 requested=arm64 target=macos-arm64 mach=arm64 reexec=0 native_arm64=1 cross=0"

check "rosetta process on Apple Silicon defaults to arm64 and reexecs" \
  "$(macos_resolve_arch x86_64 1 "" 0 "")" \
  "host_hardware=arm64 process=x86_64 requested=arm64 target=macos-arm64 mach=arm64 reexec=1 native_arm64=1 cross=0"

expect_fail "intel host default is not a packaging target" \
  macos_resolve_arch x86_64 0 "" 0 ""

expect_fail "amd64 request is not a packaging target" \
  macos_resolve_arch amd64 0 macos-x86_64 0 ""

expect_fail "explicit x86_64 on Apple Silicon is rejected" \
  macos_resolve_arch arm64 1 macos-x86_64 0 ""

expect_fail "intel host requesting arm64 does not fall back" \
  macos_resolve_arch x86_64 0 macos-arm64 0 ""

expect_fail "cross flag does not authorize an ARM64 build on Intel" \
  macos_resolve_arch x86_64 0 macos-arm64 1 ""

expect_fail "invalid architecture string" \
  macos_resolve_arch ppc 0 "" 0 ""

expect_fail "conflicting RIGOR_MACOS_ARCH" \
  macos_resolve_arch arm64 1 macos-arm64 0 x86_64

expect_fail "empty process architecture" \
  macos_resolve_arch "" unknown "" 0 ""

header_ver="$(macos_release_version "${ROOT}")" || exit 1
check "header display version" "${header_ver}" "0.2.3"
if RIGOR_RELEASE_VERSION=0.2 macos_release_version "${ROOT}" >/dev/null; then
  echo "FAIL stale RIGOR_RELEASE_VERSION 0.2 was accepted" >&2
  fail=1
else
  echo "PASS stale RIGOR_RELEASE_VERSION 0.2 is rejected"
fi
if RIGOR_RELEASE_VERSION=v0.2.3 macos_release_version "${ROOT}" >/dev/null; then
  echo "FAIL leading v in RIGOR_RELEASE_VERSION was accepted" >&2
  fail=1
else
  echo "PASS leading v in RIGOR_RELEASE_VERSION is rejected"
fi

tmp="$(mktemp -d "${TMPDIR:-/tmp}/rigor-macos-arch.XXXXXX")"
cleanup() { rm -rf "${tmp}"; }
trap cleanup EXIT

check "missing build dir is reusable" \
  "$(macos_build_dir_reusable "${tmp}/none" arm64)" \
  "OK"

mkdir -p "${tmp}/arm"
printf '%s\n' arm64 > "${tmp}/arm/RIGOR_ARCH_STAMP"
check "matching stamp is reusable" \
  "$(macos_build_dir_reusable "${tmp}/arm" arm64)" \
  "OK"
expect_fail "stale x86_64 stamp is not reused for arm64" \
  macos_build_dir_reusable "${tmp}/arm" x86_64

mkdir -p "${tmp}/unstamped"
printf '%s\n' "cache" > "${tmp}/unstamped/config.status"
expect_fail "config.status without a stamp is not reused" \
  macos_build_dir_reusable "${tmp}/unstamped" arm64

# On a non-macOS host the DMG script must stop before it configures core/.
# On Darwin this check is skipped so the test does not start a compile.
if [[ "$(uname -s)" != "Darwin" ]]; then
  core_stamp=""
  if [[ -f "${ROOT}/core/config.status" ]]; then
    core_stamp="$(stat -c %y "${ROOT}/core/config.status" 2>/dev/null || stat -f %m "${ROOT}/core/config.status")"
  fi
  set +e
  host_out="$(bash "${ROOT}/tools/release/build-macos-wallet-dmg.sh" 2>&1)"
  host_rc=$?
  set -e
  if [[ "${host_rc}" -eq 0 ]]; then
    echo "FAIL non-Darwin host was accepted" >&2
    fail=1
  elif ! grep -q "require Darwin" <<< "${host_out}"; then
    echo "FAIL non-Darwin rejection did not name Darwin" >&2
    echo "${host_out}" >&2
    fail=1
  else
    echo "PASS non-Darwin host is rejected"
  fi
  if [[ -n "${core_stamp}" ]]; then
    now="$(stat -c %y "${ROOT}/core/config.status" 2>/dev/null || stat -f %m "${ROOT}/core/config.status")"
    if [[ "${now}" != "${core_stamp}" ]]; then
      echo "FAIL core/config.status changed" >&2
      fail=1
    else
      echo "PASS core/config.status was not modified"
    fi
  fi
else
  echo "SKIP non-Darwin rejection because this host is Darwin"
fi

if [[ "${fail}" -ne 0 ]]; then
  echo "MACOS_ARCH_SELECTION_TESTS=FAIL" >&2
  exit 1
fi
echo "MACOS_ARCH_SELECTION_TESTS=PASS"
echo "NATIVE_APPLE_SILICON_ACCEPTANCE=NOT_CLAIMED"
