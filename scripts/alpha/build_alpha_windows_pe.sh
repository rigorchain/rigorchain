#!/usr/bin/env bash
# / Windows PE — cross-build real PE binaries
# No consensus / RFC / RES model changes.
#
# Produces:
#   dist/alpha/windows/staging/rigor-wallet.exe
#   dist/alpha/windows/staging/rigord.exe
#   dist/alpha/windows/staging/rigor-miner.exe
#   dist/alpha/windows/staging/rigor-cli.exe (optional)
#
# Requires: mingw-w64, make, network for depends downloads.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CORE="${REPO_ROOT}/core"
DEPENDS="${CORE}/depends"
HOST=x86_64-w64-mingw32
STAGING="${REPO_ROOT}/dist/alpha/windows/staging"
JOBS="${JOBS:-$(nproc)}"
SKIP_DEPENDS="${SKIP_DEPENDS:-0}"
SKIP_CORE="${SKIP_CORE:-0}"
SKIP_MINER="${SKIP_MINER:-0}"

log() { printf '[win-pe] %s\n' "$*"; }

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { log "ERROR: missing $1"; exit 1; }
}

need_cmd make
need_cmd "${HOST}-g++"
need_cmd "${HOST}-gcc"

mkdir -p "${STAGING}"

# --- depends (Boost, Qt, libevent, curl, …) ---
if [[ "${SKIP_DEPENDS}" != "1" ]]; then
  log "Building depends HOST=${HOST} (long)…"
  make -C "${DEPENDS}" HOST="${HOST}" -j"${JOBS}"
else
  log "Skipping depends (SKIP_DEPENDS=1)"
fi

CONFIG_SITE="${DEPENDS}/${HOST}/share/config.site"
if [[ ! -f "${CONFIG_SITE}" ]]; then
  log "ERROR: ${CONFIG_SITE} missing — depends incomplete"
  exit 1
fi

# libsodium is a depends package (depends/packages/libsodium.mk) pinned to
# tools/release/libsodium.lock. A prefix built before the pin may hold another version.
# shellcheck source=../../tools/release/libsodium.lock
source "${REPO_ROOT}/tools/release/libsodium.lock"
SODIUM_PC="${DEPENDS}/${HOST}/lib/pkgconfig/libsodium.pc"
if [[ ! -f "${SODIUM_PC}" ]]; then
  log "ERROR: ${SODIUM_PC} missing — rebuild depends or run build_alpha_windows_libsodium.sh"
  exit 1
fi
SODIUM_HAVE="$(PKG_CONFIG_PATH="${DEPENDS}/${HOST}/lib/pkgconfig" pkg-config --modversion libsodium)"
if [[ "${SODIUM_HAVE}" != "${LIBSODIUM_VERSION}" ]]; then
  log "ERROR: depends prefix has libsodium ${SODIUM_HAVE}; pinned ${LIBSODIUM_VERSION}"
  log "  remove ${DEPENDS}/${HOST}/lib/libsodium.a and its pkgconfig file, then rebuild depends"
  exit 1
fi

# --- Core PE (rigord, rigor-cli, rigor-qt → rigor-wallet) ---
# Out-of-tree configure requires an unconfigured source tree. If Linux in-tree
# config exists, distclean first (Linux bins remain in dist/alpha/linux/).
if [[ "${SKIP_CORE}" != "1" ]]; then
  log "Configuring + building core for ${HOST}…"
  cd "${CORE}"
  if [[ ! -f ./configure ]]; then
    ./autogen.sh
  fi
  if [[ -f ./config.status ]]; then
    log "In-tree config.status present — make distclean (required for mingw VPATH/host switch)"
    make distclean || true
  fi
  BUILD_DIR="${CORE}/build-mingw-alpha"
  rm -rf "${BUILD_DIR}"
  mkdir -p "${BUILD_DIR}"
  cd "${BUILD_DIR}"
  # CSW-1: production artifacts are mainnet-only. CXXFLAGS works even before autogen
  # regenerates --enable-production-mainnet.
  prod_flag=()
  if "${CORE}/configure" --help 2>/dev/null | grep -q enable-production-mainnet; then
    prod_flag=(--enable-production-mainnet)
  fi
  CXXFLAGS="${CXXFLAGS:-} -DRIGOR_PRODUCTION_MAINNET" \
  CONFIG_SITE="${CONFIG_SITE}" "${CORE}/configure" \
    --prefix=/ \
    --host="${HOST}" \
    "${prod_flag[@]}" \
    --enable-static-libsodium \
    --with-gui=qt5 \
    --enable-reduce-exports \
    --disable-bench \
    --disable-tests \
    --disable-gui-tests \
    --without-miniupnpc \
    --without-natpmp \
    --without-libs
  make -j"${JOBS}"
  for cand in \
    "${BUILD_DIR}/src/qt/rigor-qt.exe" \
    "${BUILD_DIR}/src/qt/.libs/rigor-qt.exe"; do
    [[ -f "$cand" ]] && cp -a "$cand" "${STAGING}/rigor-wallet.exe" && break
  done
  for cand in \
    "${BUILD_DIR}/src/rigord.exe" \
    "${BUILD_DIR}/src/.libs/rigord.exe"; do
    [[ -f "$cand" ]] && cp -a "$cand" "${STAGING}/rigord.exe" && break
  done
  for cand in \
    "${BUILD_DIR}/src/rigor-cli.exe" \
    "${BUILD_DIR}/src/.libs/rigor-cli.exe"; do
    [[ -f "$cand" ]] && cp -a "$cand" "${STAGING}/rigor-cli.exe" && break
  done
else
  log "Skipping core (SKIP_CORE=1)"
fi

# --- Miner PE ---
if [[ "${SKIP_MINER}" != "1" ]]; then
  log "Cross-building rigor-miner…"
  MINER_SRC="${REPO_ROOT}/miner"
  if [[ ! -d "${MINER_SRC}" && -d "${REPO_ROOT}/cpuminer" ]]; then
    MINER_SRC="${REPO_ROOT}/cpuminer"
  fi
  if [[ ! -d "${MINER_SRC}" ]]; then
    log "ERROR: miner source missing (expected miner/ or cpuminer/)"
    exit 1
  fi
  # Out-of-tree copy so miner/ stays a Linux-safe source tree.
  MINER="${REPO_ROOT}/.runtime/miner-mingw"
  mkdir -p "${REPO_ROOT}/.runtime"
  rm -rf "${MINER}"
  cp -a "${MINER_SRC}" "${MINER}"
  cd "${MINER}"
  [[ -f configure ]] || ./autogen.sh
  if [[ -f ./config.status ]]; then
    make distclean || true
  fi
  # libcurl must already be in depends prefix (scripts/alpha/build_alpha_windows_curl.sh).
  if [[ ! -f "${DEPENDS}/${HOST}/lib/libcurl.a" ]]; then
    log "ERROR: ${DEPENDS}/${HOST}/lib/libcurl.a missing — run scripts/alpha/build_alpha_windows_curl.sh"
    exit 1
  fi
  # CURL_STATICLIB required for static libcurl. Schannel deps go in LIBS so
  # AC_CHECK_LIB(curl) can resolve curl_easy_init. Unset CONFIG_SITE.
  # Static-link winpthread so rigor-miner.exe does not need libwinpthread-1.dll on Windows.
  CURL_WIN_LIBS="-lws2_32 -lcrypt32 -lbcrypt -lsecur32 -lwldap32 -lwinmm"
  env -u CONFIG_SITE ./configure --host="${HOST}" --build="$(gcc -dumpmachine 2>/dev/null || echo x86_64-pc-linux-gnu)" \
    CC="${HOST}-gcc" CXX="${HOST}-g++" \
    CPPFLAGS="-DCURL_STATICLIB -I${DEPENDS}/${HOST}/include" \
    LDFLAGS="-L${DEPENDS}/${HOST}/lib -static-libgcc" \
    LIBS="${CURL_WIN_LIBS}" \
    PTHREAD_LIBS="-Wl,-Bstatic -lpthread -lwinpthread -Wl,-Bdynamic" \
    2>&1 | tee /tmp/minerd-mingw-configure.log | tail -40
  make -j"${JOBS}"
  # automake injects -lpthread (DLL). Always relink pthread/curl static.
  # Do not `objdump | grep` under pipefail: SIGPIPE makes the check look false.
  log "Relinking miner with static libcurl/winpthread…"
  rm -f minerd.exe
  miner_objs=()
  for o in minerd-cpu-miner.o minerd-util.o minerd-sha2.o minerd-scrypt.o \
           minerd-rgrpow.o minerd-gbt_solo.o minerd-arith_uint256.o \
           minerd-sha2-x64.o minerd-scrypt-x64.o; do
    if [[ ! -f "$o" ]]; then
      log "ERROR: missing object $o after mingw make"
      exit 1
    fi
    miner_objs+=("$o")
  done
  x86_64-w64-mingw32-g++ -g -O2 \
    -L"${DEPENDS}/${HOST}/lib" -static-libgcc -static-libstdc++ \
    -o minerd.exe \
    "${miner_objs[@]}" \
    compat/jansson/libjansson.a \
    -Wl,-Bstatic -lcurl -lpthread -lwinpthread \
    -Wl,-Bdynamic \
    ${CURL_WIN_LIBS}
  miner_dlls="$(x86_64-w64-mingw32-objdump -p minerd.exe | awk '/DLL Name:/{print $3}')"
  if echo "${miner_dlls}" | grep -qiE 'libwinpthread|libgcc_s|libstdc\+\+'; then
    log "ERROR: rigor-miner still depends on MinGW runtime DLLs: ${miner_dlls}"
    exit 1
  fi
  for cand in "${MINER}/minerd.exe"; do
    if [[ -f "$cand" ]]; then
      cp -a "$cand" "${STAGING}/rigor-miner.exe"
      break
    fi
  done
  if [[ ! -f "${STAGING}/rigor-miner.exe" ]]; then
    log "ERROR: miner PE missing — see /tmp/minerd-mingw-configure.log"
    exit 1
  fi
  cp -a "${REPO_ROOT}/installer/windows/rigor-miner.cmd" "${STAGING}/rigor-miner.cmd"
  log "staged rigor-miner.cmd (RFC-020 SOLO launcher)"
fi

# Dashboard ships next to rigor-miner (not in the wallet installer).
if [[ -f "${STAGING}/rigor-miner.exe" ]] || [[ -f "${STAGING}/rigor-miner.cmd" ]]; then
  bash "${REPO_ROOT}/scripts/alpha/stage_windows_miner_dashboard.sh" "${STAGING}"
fi

# --- PE gate ---
if [[ "${SKIP_CORE}" == "1" ]]; then
  log "Skipping full staging PE gate (SKIP_CORE=1); verifying miner PE only…"
  if [[ ! -f "${STAGING}/rigor-miner.exe" ]]; then
    log "ERROR: ${STAGING}/rigor-miner.exe missing"
    exit 1
  fi
  magic="$(od -An -tx1 -N2 "${STAGING}/rigor-miner.exe" | tr -d ' \n')"
  if [[ "${magic}" != "4d5a" ]]; then
    log "ERROR: rigor-miner.exe is not a Windows MZ/PE"
    exit 1
  fi
  miner_dlls="$(x86_64-w64-mingw32-objdump -p "${STAGING}/rigor-miner.exe" | awk '/DLL Name:/{print $3}')"
  if echo "${miner_dlls}" | grep -qiE 'libwinpthread|libgcc_s|libstdc\+\+'; then
    log "ERROR: rigor-miner still depends on MinGW runtime DLLs: ${miner_dlls}"
    exit 1
  fi
  log "Miner PE OK:"
  ls -la "${STAGING}/rigor-miner.exe"
else
  log "Verifying PE staging…"
  bash "${REPO_ROOT}/scripts/alpha/verify_windows_pe.sh" "${STAGING}"
  log "Staging complete:"
  ls -la "${STAGING}"
fi
