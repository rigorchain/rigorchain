#!/usr/bin/env bash
# Stage Windows wallet NSIS + node/cli PEs into dist/release/v${VER}/staging/windows
# Wallet package only (SKIP_MINER=1). Does not publish. Does not include miner.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
# shellcheck source=official_packaging.inc.sh
source "${ROOT}/tools/release/official_packaging.inc.sh"
official_init

die() { echo "FAIL: $*" >&2; exit 1; }
log() { printf '[stage-windows] %s\n' "$*"; }

PE_STAGING="${ROOT}/dist/alpha/windows/staging"
OUT="${STAGING}/windows"
NSI="${ROOT}/installer/windows/rigor_alpha_installer.nsi"
mkdir -p "${OUT}"

is_mz() {
  local f="$1"
  [[ -f "$f" ]] || return 1
  local magic
  magic="$(od -An -tx1 -N2 "$f" | tr -d ' \n')"
  [[ "${magic}" == "4d5a" ]]
}

wallet_pes_ok() {
  is_mz "${PE_STAGING}/rigor-wallet.exe" \
    && is_mz "${PE_STAGING}/rigord.exe" \
    && is_mz "${PE_STAGING}/rigor-cli.exe"
}

if [[ "${FORCE:-0}" != "1" ]] && is_mz "${OUT}/${WIN_EXE_NAME}" && is_mz "${OUT}/rigord.exe" && is_mz "${OUT}/rigor-cli.exe"; then
  log "reusing ${OUT}"
  ls -lh "${OUT}/${WIN_EXE_NAME}" "${OUT}/rigord.exe" "${OUT}/rigor-cli.exe"
  exit 0
fi

if [[ "${FORCE:-0}" == "1" ]] || ! wallet_pes_ok; then
  command -v x86_64-w64-mingw32-g++ >/dev/null || die "missing mingw (x86_64-w64-mingw32-g++)"
  log "building wallet PEs (SKIP_MINER=1); miner verify FAIL is expected"
  set +e
  SKIP_DEPENDS="${SKIP_DEPENDS:-1}" SKIP_MINER=1 \
    bash "${ROOT}/scripts/alpha/build_alpha_windows_pe.sh"
  pe_rc=$?
  set -e
  wallet_pes_ok || die "wallet PEs missing after PE build (exit ${pe_rc})"
  log "wallet PEs present (PE script exit ${pe_rc} ignored if miner-only fail)"
fi

command -v makensis >/dev/null || die "makensis not found"
x86_64-w64-mingw32-strip --strip-unneeded \
  "${PE_STAGING}/rigor-wallet.exe" "${PE_STAGING}/rigord.exe" "${PE_STAGING}/rigor-cli.exe" || true

mkdir -p "${PE_STAGING}/internal"
if [[ -f "${PE_STAGING}/internal/rigor-qt.exe" ]]; then
  :
elif [[ -f "${PE_STAGING}/rigor-qt.exe" ]]; then
  mv "${PE_STAGING}/rigor-qt.exe" "${PE_STAGING}/internal/rigor-qt.exe"
elif [[ -f "${PE_STAGING}/rigor-wallet.exe" ]]; then
  # Existing staging stored the GUI as rigor-wallet.exe.
  cp -a "${PE_STAGING}/rigor-wallet.exe" "${PE_STAGING}/internal/rigor-qt.exe"
fi
[[ -f "${PE_STAGING}/internal/rigor-qt.exe" ]] || die "missing GUI PE for internal/rigor-qt.exe"
command -v x86_64-w64-mingw32-gcc >/dev/null || die "missing x86_64-w64-mingw32-gcc"
command -v x86_64-w64-mingw32-windres >/dev/null || die "missing x86_64-w64-mingw32-windres"
ICO="${ROOT}/core/src/qt/res/icons/rigor.ico"
[[ -f "${ICO}" ]] || die "missing ${ICO}"
x86_64-w64-mingw32-windres \
  -I "${ROOT}/core/src/qt/res/icons" \
  -O coff \
  -o "${PE_STAGING}/official-wallet-launcher.res" \
  "${ROOT}/tools/release/official-wallet-launcher.rc"
x86_64-w64-mingw32-gcc -O2 -s -mwindows \
  -o "${PE_STAGING}/rigor-wallet.exe" \
  "${ROOT}/tools/release/official-wallet-launcher.c" \
  "${PE_STAGING}/official-wallet-launcher.res"

cat > "${PE_STAGING}/DATADIR.txt" <<EOF
RIGOR data directory (Windows)
Always: <this-install>\\data
rigor.conf: <this-install>\\data\\rigor.conf
This follows the wallet program. It does not reuse a previous install path from the registry.
EOF
cat > "${PE_STAGING}/README.txt" <<EOF
Rigor Wallet v${RELEASE_VERSION} (Windows x86_64)
Official installer name: ${WIN_EXE_NAME}
ALWAYS start rigor-wallet.exe. Do not open files inside internal\\.
Datadir: folder named data next to rigor-wallet.exe
Packed CLIENT_VERSION remains 25.0.0.
EOF
cat > "${PE_STAGING}/rigor.conf.template" <<'EOF'
server=1
txindex=1
listen=1
rpcallowip=127.0.0.1
EOF

IFS=. read -r vi_a vi_b vi_c vi_d _ <<< "${RELEASE_VERSION}.0.0.0"
makensis -V2 \
  -DREPO_ROOT="${ROOT}" \
  -DSTAGING_DIR="${PE_STAGING}" \
  -DOUTFILE="${OUT}/${WIN_EXE_NAME}" \
  -DPRODUCT_VERSION="${RELEASE_VERSION}" \
  -DVI_PRODUCT_VERSION="${vi_a}.${vi_b}.${vi_c}.${vi_d}" \
  "${NSI}"

is_mz "${OUT}/${WIN_EXE_NAME}" || die "NSIS outfile not MZ"
cp -a "${PE_STAGING}/rigord.exe" "${PE_STAGING}/rigor-cli.exe" "${PE_STAGING}/rigor-wallet.exe" "${OUT}/"
log "STAGED=${OUT}/${WIN_EXE_NAME}"
