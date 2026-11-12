#!/usr/bin/env bash
# Build a Rigor Wallet .dmg on macOS.
# Wallet + node + CLI only. Does not ship a miner.
#
# Host architecture, requested target, and the Mach-O architecture are
# reported separately. The only macOS packaging target is ARM64. Apple Silicon
# defaults to ARM64, including when the shell is running under Rosetta. A
# requested ARM64 target is never silently rewritten to x86_64. An Intel host
# fails closed.
#
# Official output is dist/release/v${VERSION}/macos-arm64/.
# VERSION comes from RIGOR_RELEASE_VERSION and must match clientversion.h.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=macos_arch.sh
source "${ROOT}/tools/release/macos_arch.sh"

CORE="${ROOT}/core"
VERSION="$(macos_release_version "${ROOT}")" || exit 1

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "FAIL: macOS wallet DMG builds require Darwin. This host is $(uname -s) $(uname -m). An x86_64 Linux build is not Apple Silicon output." >&2
  exit 1
fi

command -v sysctl >/dev/null 2>&1 || {
  echo "FAIL: sysctl is required to distinguish Apple Silicon from Intel" >&2
  exit 1
}
hw_raw="$(sysctl -n hw.optional.arm64 2>/dev/null || true)"
case "${hw_raw}" in
  1) hw_flag=1 ;;
  0) hw_flag=0 ;;
  "")
    echo "FAIL: hw.optional.arm64 is empty. Refusing to treat uname as proof of Apple Silicon hardware." >&2
    exit 1
    ;;
  *)
    echo "FAIL: unexpected hw.optional.arm64 value '${hw_raw}'" >&2
    exit 1
    ;;
esac

resolved="$(macos_resolve_arch "$(uname -m)" "${hw_flag}" "${RIGOR_MACOS_TARGET:-}" 0 "${RIGOR_MACOS_ARCH:-}")" || exit 1
# shellcheck disable=SC2086
eval "${resolved}"

if [[ "${reexec}" == "1" && "${RIGOR_MACOS_REEXECED:-}" != "1" ]]; then
  command -v arch >/dev/null 2>&1 || {
    echo "FAIL: the shell is not arm64 on Apple Silicon. arch is required to re-exec natively." >&2
    exit 1
  }
  export RIGOR_MACOS_REEXECED=1
  exec arch -arm64 /usr/bin/env bash "${BASH_SOURCE[0]}" "$@"
fi
if [[ "${mach}" != "arm64" ]]; then
  echo "FAIL: macOS packaging target must be arm64, got ${mach}" >&2
  exit 1
fi
if [[ "$(uname -m)" != "arm64" ]]; then
  echo "FAIL: ARM64 target is still running as $(uname -m) after re-exec" >&2
  exit 1
fi

TARGET="${target}"
BUILD_DIR="${RIGOR_MACOS_BUILD_DIR:-${ROOT}/dist/macos-build/${TARGET}}"
macos_build_dir_reusable "${BUILD_DIR}" "${mach}" >/dev/null

OUT="${ROOT}/dist/release/v${VERSION}/${TARGET}"
mkdir -p "${OUT}" "${BUILD_DIR}"
printf '%s\n' "${mach}" > "${BUILD_DIR}/RIGOR_ARCH_STAMP"

echo "MACOS_WALLET_DMG=START"
echo "HOST_HARDWARE=${host_hardware}"
echo "PROCESS_ARCH=$(uname -m)"
echo "REQUESTED_TARGET=${requested}"
echo "TARGET=${TARGET}"
echo "MACH=${mach}"
echo "NATIVE_APPLE_SILICON_ACCEPTANCE=$([[ "${native_arm64}" == "1" && "$(uname -m)" == "arm64" ]] && echo yes || echo no)"
echo "CROSS=${cross}"
echo "BUILD_DIR=${BUILD_DIR}"
echo "RELEASE_VERSION=${VERSION}"
echo "GIT_COMMIT=$(git -C "${ROOT}" rev-parse HEAD 2>/dev/null || echo unknown)"

command -v xcrun >/dev/null 2>&1 || {
  echo "FAIL: xcrun is required. Missing SDK selection is not an architecture default." >&2
  exit 1
}
SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
SDK_VERSION="$(xcrun --sdk macosx --show-sdk-version)"
export SDKROOT
echo "SDKROOT=${SDKROOT}"
echo "SDK_VERSION=${SDK_VERSION}"

CXX_BIN="${CXX:-clang++}"
command -v "${CXX_BIN}" >/dev/null 2>&1 || {
  echo "FAIL: missing compiler ${CXX_BIN}" >&2
  exit 1
}
echo "COMPILER=${CXX_BIN}"
echo "COMPILER_VERSION=$("${CXX_BIN}" --version 2>/dev/null | head -n 1 || true)"
echo "COMPILER_TARGET=$("${CXX_BIN}" -dumpmachine 2>/dev/null || true)"
echo "COMPILER_ARCH_FLAG=-arch ${mach}"

# macOS 15 system Python is PEP 668 managed. macdeployqtplus needs ds_store
# and mac_alias. Use a venv; do not pip --user or --break-system-packages.
VENV="${RIGOR_MACOS_DMG_VENV:-${ROOT}/.venv-macos-dmg}"
if [[ ! -x "${VENV}/bin/python3" ]]; then
  python3 -m venv "${VENV}"
  "${VENV}/bin/pip" install --upgrade pip
  "${VENV}/bin/pip" install ds_store mac_alias
fi
export PATH="${VENV}/bin:${PATH}"
export PYTHON="${VENV}/bin/python3"
"${PYTHON}" -c "import ds_store, mac_alias"

QT5_PREFIX="$(brew --prefix qt@5 2>/dev/null || true)"
SODIUM_PREFIX="${ROOT}/.runtime/libsodium-pinned-macos-${mach}"
bash "${ROOT}/tools/release/fetch-libsodium.sh" --prefix "${SODIUM_PREFIX}"
BOOST_PREFIX="$(brew --prefix boost 2>/dev/null || true)"
if [[ -n "${QT5_PREFIX}" ]]; then
  export PATH="${QT5_PREFIX}/bin:${PATH}"
  export PKG_CONFIG_PATH="${QT5_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
  echo "QT5_PREFIX=${QT5_PREFIX}"
  echo "QT5_VERSION=$(brew list --versions qt@5 2>/dev/null || true)"
fi
if [[ -n "${SODIUM_PREFIX}" ]]; then
  export PKG_CONFIG_PATH="${SODIUM_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
fi
if [[ -n "${BOOST_PREFIX}" ]]; then
  export PKG_CONFIG_PATH="${BOOST_PREFIX}/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
fi

need() { command -v "$1" >/dev/null 2>&1 || { echo "FAIL: missing $1" >&2; exit 1; }; }
need g++
need make
need pkg-config
need python3
pkg-config --exists libsodium || { echo "FAIL: libsodium.pc missing"; exit 1; }

# Fresh GitHub checkouts omit these dummy Makefiles because `Makefile` is
# gitignored. autoreconf / AC_CONFIG_LINKS requires them. Do not configure
# inside an already configured core/ tree.
mkdir -p "${CORE}/src/qt/test" "${CORE}/src/test"
if [[ ! -f "${CORE}/src/qt/Makefile" ]]; then
  cat > "${CORE}/src/qt/Makefile" <<'EOF'
.PHONY: FORCE
all: FORCE
	$(MAKE) -C .. bitcoin_qt test_bitcoin_qt
clean: FORCE
	$(MAKE) -C .. bitcoin_qt_clean test_bitcoin_qt_clean
check: FORCE
	$(MAKE) -C .. test_bitcoin_qt_check
bitcoin-qt bitcoin-qt.exe rigor-qt rigor-qt.exe: FORCE
	$(MAKE) -C .. bitcoin_qt
apk: FORCE
	$(MAKE) -C .. bitcoin_qt_apk
EOF
fi
if [[ ! -f "${CORE}/src/qt/test/Makefile" ]]; then
  cat > "${CORE}/src/qt/test/Makefile" <<'EOF'
all:
	$(MAKE) -C ../../ test_bitcoin_qt
clean:
	$(MAKE) -C ../../ test_bitcoin_qt_clean
check:
	$(MAKE) -C ../../ test_bitcoin_qt_check
EOF
fi
if [[ ! -f "${CORE}/src/test/Makefile" ]]; then
  cat > "${CORE}/src/test/Makefile" <<'EOF'
all:
	$(MAKE) -C .. bitcoin_test
clean:
	$(MAKE) -C .. bitcoin_test_clean
check:
	$(MAKE) -C .. bitcoin_test_check
EOF
fi

if [[ ! -x "${CORE}/configure" ]]; then
  (cd "${CORE}" && bash ./autogen.sh)
fi

qt_args=()
if [[ -n "${QT5_PREFIX}" ]]; then
  export QTDIR="${QT5_PREFIX}"
  qt_args+=(--with-qt-bindir="${QT5_PREFIX}/bin")
  trans=""
  for cand in \
    "${QT5_PREFIX}/share/qt/translations" \
    "${QT5_PREFIX}/share/qt5/translations" \
    "${QT5_PREFIX}/lib/qt5/translations"
  do
    if [[ -d "${cand}" ]]; then
      trans="${cand}"
      break
    fi
  done
  if [[ -z "${trans}" ]]; then
    trans="$(find "${QT5_PREFIX}" -type d -name translations 2>/dev/null | head -n1 || true)"
  fi
  if [[ -n "${trans}" ]]; then
    qt_args+=(--with-qt-translationdir="${trans}")
    echo "QT_TRANSLATIONS=${trans}"
  fi
fi
prod_flag=()
if "${CORE}/configure" --help 2>/dev/null | grep -q enable-production-mainnet; then
  prod_flag=(--enable-production-mainnet)
fi

arch_flags="-arch ${mach}"
export CFLAGS="${arch_flags} ${CFLAGS:-}"
export CXXFLAGS="${arch_flags} -O2 -DRIGOR_PRODUCTION_MAINNET ${CXXFLAGS:-}"
export OBJCXXFLAGS="${arch_flags} -O2 ${OBJCXXFLAGS:-}"
export LDFLAGS="${arch_flags} ${LDFLAGS:-}"

(
  cd "${BUILD_DIR}"
  "${CORE}/configure" \
    --enable-static-libsodium \
    --without-bdb \
    --with-sqlite=yes \
    --with-gui=qt5 \
    "${qt_args[@]}" \
    "${prod_flag[@]}" \
    --disable-bench \
    --disable-tests \
    --disable-gui-tests \
    --disable-fuzz-binary \
    --disable-man \
    PYTHON="${PYTHON}"
  echo "QT_DEPENDENCY_PREP=macdeployqtplus"
  make -j"$(sysctl -n hw.ncpu)"
  # macdeployqtplus rewrites Qt plugin install names, then
  # macos_bundle_inspect.py runs before hdiutil creates the DMG.
  make deploy PYTHON="${PYTHON}"
)

echo "BUNDLE_INSPECT=START"
DEPLOYED_APP="${BUILD_DIR}/dist/Rigor-Qt.app"
[[ -d "${DEPLOYED_APP}" ]] || {
  echo "FAIL: deployed bundle ${DEPLOYED_APP} was not produced. The pre-deploy app is not inspected." >&2
  exit 1
}
"${PYTHON}" "${ROOT}/tools/release/macos_bundle_inspect.py" --bundle "${DEPLOYED_APP}"

dmg_count=0
dmg_built=""
while IFS= read -r -d '' dmg_one; do
  dmg_count=$((dmg_count + 1))
  dmg_built="${dmg_one}"
done < <(find "${BUILD_DIR}" -maxdepth 3 -type f -name '*.dmg' -print0)
if [[ "${dmg_count}" -ne 1 ]]; then
  echo "FAIL: expected exactly one DMG in ${BUILD_DIR}, found ${dmg_count}. Refusing to guess among stale images." >&2
  exit 1
fi

official="rigor-v${VERSION}-${TARGET}.dmg"
cp -a "${dmg_built}" "${OUT}/${official}"
if command -v shasum >/dev/null 2>&1; then
  echo "SHA256=$(shasum -a 256 "${OUT}/${official}" | awk '{print $1}')"
fi
echo "APP_ARCH=${mach}"
echo "PACKAGED=${OUT}/${official}"
echo "MACOS_WALLET_DMG=PASS"
