#!/usr/bin/env bash
# V5 Windows (mingw-w64) cross build — dual-PoW node for wallet bundle.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$SRC/../.." && pwd)"
BUILD="${V5_BUILD_MINGW:-$ROOT/build-mingw-x86_64}"
DIST="${V5_DIST_WIN:-$ROOT/dist/win64}"
WIN="${V5_WIN:-$ROOT/hobbyhash-clean/WIN}"
HOST=x86_64-w64-mingw32
DEPENDS="$SRC/depends"
TOOLCHAIN="$DEPENDS/$HOST/toolchain.cmake"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

log() { echo "==> $*"; }

if ! command -v "${HOST}-g++" >/dev/null 2>&1; then
  echo "Missing ${HOST}-g++. Install: dnf install mingw64-gcc-c++ mingw64-gcc mingw64-winpthreads" >&2
  exit 1
fi

if [[ ! -x "/usr/bin/${HOST}-g++-posix" ]]; then
  if [[ -x "/usr/bin/${HOST}-g++" ]]; then
    ln -sf "/usr/bin/${HOST}-g++" "/usr/bin/${HOST}-g++-posix"
    ln -sf "/usr/bin/${HOST}-gcc" "/usr/bin/${HOST}-gcc-posix"
  fi
fi

log "Host: $(. /etc/os-release && echo "$PRETTY_NAME")"
log "Cross compiler: $(${HOST}-g++ --version | head -1)"
log "Source: $SRC"

if [[ ! -f "$TOOLCHAIN" ]]; then
  log "Building depends for $HOST (NO_QT=1, first run may take a while)"
  env -u CC -u CXX -u CPP -u LD -u AR -u RANLIB -u STRIP \
    make -C "$DEPENDS" HOST="$HOST" NO_QT=1 -j"$JOBS"
fi

if [[ ! -f "$TOOLCHAIN" ]]; then
  echo "Depends toolchain missing: $TOOLCHAIN" >&2
  exit 1
fi

# RandomX (V6 CPU PoW) must be cross-compiled to a PE/COFF static lib for the
# Windows node; the default in-tree librandomx.a is a Linux ELF lib. Build a
# portable (ARCH=default, no -march=native -> AVX-free, runs on all CPUs) mingw
# lib and point the node link at it via HOBC_RANDOMX_LIB.
RANDOMX_SRC="$SRC/src/crypto/randomx"
RANDOMX_MINGW_BUILD="$RANDOMX_SRC/build-mingw"
RANDOMX_MINGW_LIB="$RANDOMX_MINGW_BUILD/librandomx.a"
if [[ ! -f "$RANDOMX_MINGW_LIB" ]]; then
  log "Cross-building portable mingw librandomx.a (ARCH=default)"
  cmake -S "$RANDOMX_SRC" -B "$RANDOMX_MINGW_BUILD" \
    --toolchain "$TOOLCHAIN" \
    -DARCH=default \
    -DCMAKE_BUILD_TYPE=Release
  cmake --build "$RANDOMX_MINGW_BUILD" --parallel "$JOBS" --target randomx
fi
if [[ ! -f "$RANDOMX_MINGW_LIB" ]]; then
  echo "mingw RandomX lib missing: $RANDOMX_MINGW_LIB" >&2
  exit 1
fi

log "CMake configure (Windows Release)"
cmake -S "$SRC" -B "$BUILD" \
  --toolchain "$TOOLCHAIN" \
  -DCMAKE_BUILD_TYPE=Release \
  -DHOBC_RANDOMX_LIB="$RANDOMX_MINGW_LIB" \
  -DBUILD_DAEMON=ON \
  -DBUILD_CLI=ON \
  -DBUILD_GUI=OFF \
  -DBUILD_TESTS=OFF \
  -DBUILD_BENCH=OFF \
  -DBUILD_FUZZ_BINARY=OFF \
  -DBUILD_TX=ON \
  -DBUILD_UTIL=ON \
  -DBUILD_WALLET=ON \
  -DBUILD_WALLET_TOOL=ON \
  -DWITH_ZMQ=OFF \
  -DENABLE_IPC=OFF

log "CMake build (hobbyhashd, hobbyhash-cli, hobbyhash-wallet)"
cmake --build "$BUILD" --parallel "$JOBS" --target hobbyhashd hobbyhash-cli hobbyhash-wallet

/bin/mkdir -p "$DIST" "$WIN"
for bin in hobbyhashd hobbyhash-cli hobbyhash-wallet; do
  found="$(find "$BUILD" -type f -name "${bin}.exe" 2>/dev/null | head -1)"
  if [[ -z "$found" ]]; then
    found="$(find "$BUILD" -type f -name "$bin" ! -name "*.exe" 2>/dev/null | head -1)"
  fi
  if [[ -n "$found" ]]; then
    /bin/cp -f "$found" "$DIST/${bin}.exe"
    /bin/cp -f "$found" "$WIN/${bin}.exe"
    echo "  $bin -> $DIST/${bin}.exe"
  else
    echo "WARN: $bin not found in $BUILD" >&2
    exit 1
  fi
done

log "Windows build complete: $DIST"
