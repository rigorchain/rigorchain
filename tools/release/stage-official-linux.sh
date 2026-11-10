#!/usr/bin/env bash
# Stage Linux wallet/node bins into dist/release/v${VER}/staging/linux/bin
# Out-of-tree native build only (does not distclean mingw / in-tree).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
# shellcheck source=official_packaging.inc.sh
source "${ROOT}/tools/release/official_packaging.inc.sh"
official_init

die() { echo "FAIL: $*" >&2; exit 1; }
log() { printf '[stage-linux] %s\n' "$*"; }

CORE="${ROOT}/core"
BUILD="${CORE}/build-linux-wallet"
OUT="${STAGING}/linux/bin"
mkdir -p "${OUT}"

need_built() {
  [[ -x "${BUILD}/src/rigord" && -x "${BUILD}/src/rigor-cli" && -x "${BUILD}/src/qt/rigor-qt" ]]
}

if [[ "${FORCE:-0}" != "1" ]] && need_built; then
  log "reusing ${BUILD}"
else
  [[ ! -f "${CORE}/config.status" ]] || die "in-tree core/config.status present; refuse to fight mingw. Use out-of-tree ${BUILD} only."
  mkdir -p "${BUILD}"
  SODIUM_PREFIX="${BUILD}/libsodium-pinned"
  bash "${ROOT}/tools/release/fetch-libsodium.sh" --prefix "${SODIUM_PREFIX}" || die "pinned libsodium build failed"
  export PKG_CONFIG_PATH="${SODIUM_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
  if [[ ! -f "${BUILD}/config.status" ]]; then
    log "configuring native Linux at ${BUILD}"
    (
      cd "${BUILD}"
      env -u CONFIG_SITE CXXFLAGS="${CXXFLAGS:-} -DRIGOR_PRODUCTION_MAINNET" \
        "${CORE}/configure" \
        --enable-static-libsodium \
        --enable-production-mainnet \
        --with-gui=qt5 \
        --disable-bench \
        --disable-tests \
        --disable-gui-tests \
        --disable-fuzz-binary \
        --without-miniupnpc \
        --without-natpmp \
        --without-libs
    )
  fi
  log "make targeted bins"
  make -j"$(nproc)" -C "${BUILD}/src" rigord rigor-cli qt/rigor-qt
fi

need_built || die "linux bins missing after build"
# ldd is not usable here: a system libzmq pulls libsodium.so in transitively even when
# the verifier is embedded statically. Check the direct dependency and the embedded copy.
for b in rigord rigor-cli qt/rigor-qt; do
  if readelf -d "${BUILD}/src/${b}" | grep -q 'NEEDED.*libsodium'; then
    die "${b} links a shared libsodium; rebuild with the pinned static copy (FORCE=1)"
  fi
done
for b in rigord qt/rigor-qt; do
  # grep -q under pipefail returns 141: nm receives SIGPIPE after the first match.
  sodium_sym="$(nm "${BUILD}/src/${b}" | grep ' T sodium_version_string$' || true)"
  [[ -n "${sodium_sym}" ]] \
    || die "${b} does not embed libsodium; rebuild with the pinned static copy (FORCE=1)"
done
cp -a "${BUILD}/src/rigord" "${OUT}/rigord"
cp -a "${BUILD}/src/rigor-cli" "${OUT}/rigor-cli"
cp -a "${BUILD}/src/qt/rigor-qt" "${OUT}/rigor-wallet"
cp -a "${BUILD}/src/qt/rigor-qt" "${OUT}/rigor-qt"
strip --strip-unneeded "${OUT}/rigord" "${OUT}/rigor-cli" "${OUT}/rigor-wallet" "${OUT}/rigor-qt"
chmod +x "${OUT}"/*
"${OUT}/rigor-cli" -version | head -4
log "STAGED=${OUT}"
