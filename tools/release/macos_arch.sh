#!/usr/bin/env bash
# Architecture selection for the macOS wallet DMG.
# This file is safe to source on any host. It does not configure, compile,
# delete build trees, or write a DMG.
#
# Host hardware, the current process architecture, and the requested target
# are separate. The only macOS packaging target is ARM64. An x86_64 request
# is rejected. A requested ARM64 target is never rewritten to x86_64.

macos_normalize_mach_arch() {
  case "${1:-}" in
    arm64|aarch64) printf '%s\n' arm64 ;;
    x86_64|amd64) printf '%s\n' x86_64 ;;
    *) return 1 ;;
  esac
}

macos_target_label() {
  case "${1:-}" in
    arm64) printf '%s\n' macos-arm64 ;;
    *) return 1 ;;
  esac
}

# Official release packaging is closed before 2026-12-01.
# Argument is YYYY-MM-DD. There is no environment override.
macos_official_packaging_allowed() {
  local today="${1:-}"
  [[ "${today}" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || return 1
  [[ "${today}" > 2026-11-30 ]]
}

# Arguments:
#   1 process uname -m
#   2 hardware flag: 1 (Apple Silicon), 0 (not Apple Silicon), unknown
#   3 requested target (empty means the Apple Silicon default)
#   4 retained for call compatibility; cross-compilation is never accepted
#   5 optional process-arch assertion (empty, or arm64/aarch64/x86_64/amd64)
#
# Prints one KEY=value line on success. Returns 1 and prints FAIL on stderr.
macos_resolve_arch() {
  local process_raw="${1:-}"
  local hw_flag="${2:-unknown}"
  local requested="${3:-}"
  local arch_env="${5:-}"
  local process hardware request_mach target reexec native_arm64 cross

  process="$(macos_normalize_mach_arch "${process_raw}")" || {
    echo "FAIL: unsupported process architecture '${process_raw}'" >&2
    return 1
  }

  case "${hw_flag}" in
    1) hardware="arm64" ;;
    0) hardware="x86_64" ;;
    unknown) hardware="${process}" ;;
    *)
      echo "FAIL: hardware flag must be 1, 0, or unknown" >&2
      return 1
      ;;
  esac

  if [[ -n "${arch_env}" ]]; then
    local asserted
    asserted="$(macos_normalize_mach_arch "${arch_env}")" || {
      echo "FAIL: unsupported RIGOR_MACOS_ARCH '${arch_env}'" >&2
      return 1
    }
    if [[ "${asserted}" != "${process}" ]]; then
      echo "FAIL: RIGOR_MACOS_ARCH=${arch_env} conflicts with process architecture ${process}" >&2
      return 1
    fi
  fi

  if [[ -z "${requested}" ]]; then
    request_mach="${hardware}"
  else
    case "${requested}" in
      macos-arm64|arm64|aarch64) request_mach="arm64" ;;
      macos-x86_64|x86_64|amd64)
        echo "FAIL: macOS x86_64 is not a packaging target" >&2
        return 1
        ;;
      *)
        echo "FAIL: unsupported requested architecture '${requested}'" >&2
        return 1
        ;;
    esac
  fi

  reexec=0
  native_arm64=0
  cross=0

  if [[ "${request_mach}" != "arm64" ]]; then
    echo "FAIL: macOS packaging target must be arm64, got '${request_mach}'" >&2
    return 1
  fi
  if [[ "${hardware}" != "arm64" ]]; then
    echo "FAIL: requested ARM64 on non-ARM hardware. Refusing to fall back to x86_64 and refusing cross-compilation." >&2
    return 1
  fi

  if [[ "${process}" != "arm64" ]]; then
    reexec=1
  fi
  native_arm64=1

  target="$(macos_target_label "${request_mach}")" || return 1
  printf 'host_hardware=%s process=%s requested=%s target=%s mach=%s reexec=%s native_arm64=%s cross=%s\n' \
    "${hardware}" "${process}" "${request_mach}" "${target}" "${request_mach}" \
    "${reexec}" "${native_arm64}" "${cross}"
}

# Refuse a build directory that was used for another architecture.
# Does not delete anything. Prints OK or returns 1.
macos_build_dir_reusable() {
  local dir="${1:-}"
  local mach="${2:-}"
  local stamp
  [[ -n "${dir}" && -n "${mach}" ]] || {
    echo "FAIL: build directory and architecture are required" >&2
    return 1
  }
  mach="$(macos_normalize_mach_arch "${mach}")" || {
    echo "FAIL: unsupported build architecture '${2}'" >&2
    return 1
  }
  if [[ ! -e "${dir}" ]]; then
    printf '%s\n' OK
    return 0
  fi
  stamp="${dir}/RIGOR_ARCH_STAMP"
  if [[ -f "${stamp}" ]]; then
    local stamped
    stamped="$(tr -d '[:space:]' < "${stamp}")"
    if [[ "${stamped}" != "${mach}" ]]; then
      echo "FAIL: ${dir} is stamped ${stamped}, not ${mach}. Choose a new RIGOR_MACOS_BUILD_DIR. This script does not delete the existing directory." >&2
      return 1
    fi
    printf '%s\n' OK
    return 0
  fi
  if [[ -e "${dir}/config.status" || -e "${dir}/config.cache" ]]; then
    echo "FAIL: ${dir} has configuration files and no RIGOR_ARCH_STAMP. Refusing to reuse it. Choose a new RIGOR_MACOS_BUILD_DIR." >&2
    return 1
  fi
  printf '%s\n' OK
}
