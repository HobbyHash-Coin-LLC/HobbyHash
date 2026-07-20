#!/usr/bin/env bash
# Build hobbyhashd for Android (NDK) and package into the Android wallet app.
# Targets match the live network node version from this source tree (currently 31.0.7).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ANDROID_WALLET_ROOT="${ANDROID_WALLET_ROOT:-$REPO_ROOT/../apps/hobbyhash-wallet-android}"
ANDROID_SDK="${ANDROID_SDK:-${ANDROID_HOME:-/opt/android-sdk}}"
ANDROID_API_LEVEL="${ANDROID_API_LEVEL:-28}"
ANDROID_NDK_VERSION="${ANDROID_NDK_VERSION:-27.2.12479018}"
ANDROID_NDK="${ANDROID_NDK:-$ANDROID_SDK/ndk/$ANDROID_NDK_VERSION}"
ANDROID_TOOLCHAIN_BIN="${ANDROID_TOOLCHAIN_BIN:-$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
JOBS="${JOBS:-2}"
# Emulator needs x86_64; phones need arm64. Override with ANDROID_HOSTS="x86_64-linux-android".
if [[ -n "${ANDROID_HOSTS:-}" ]]; then
  # shellcheck disable=SC2206
  HOSTS=($ANDROID_HOSTS)
else
  HOSTS=(x86_64-linux-android aarch64-linux-android)
fi

log() { echo "==> $*"; }

if [[ ! -d "$ANDROID_SDK" ]]; then
  echo "Android SDK not found: $ANDROID_SDK" >&2
  exit 1
fi

if [[ ! -d "$ANDROID_NDK" ]]; then
  SDKMANAGER="$ANDROID_SDK/cmdline-tools/latest/bin/sdkmanager"
  if [[ ! -x "$SDKMANAGER" && -x "$ANDROID_SDK/cmdline-tools/latest-2/bin/sdkmanager" ]]; then
    SDKMANAGER="$ANDROID_SDK/cmdline-tools/latest-2/bin/sdkmanager"
  fi
  if [[ ! -x "$SDKMANAGER" ]]; then
    echo "Android NDK not found and sdkmanager is unavailable. Install ndk;$ANDROID_NDK_VERSION." >&2
    exit 1
  fi
  "$SDKMANAGER" --install "ndk;$ANDROID_NDK_VERSION"
fi

if [[ ! -d "$ANDROID_TOOLCHAIN_BIN" ]]; then
  echo "Android toolchain bin not found: $ANDROID_TOOLCHAIN_BIN" >&2
  exit 1
fi

if [[ ! -f "$REPO_ROOT/depends/hosts/android.mk" ]]; then
  echo "Missing depends/hosts/android.mk" >&2
  exit 1
fi

build_host() {
  local host="$1"
  local build_dir="$REPO_ROOT/build-android-$host"
  local depends_prefix="$REPO_ROOT/depends/$host"
  local toolchain="$depends_prefix/toolchain.cmake"
  local assets_abi
  local jni_abi

  case "$host" in
    aarch64-linux-android) assets_abi="arm64-v8a"; jni_abi="arm64-v8a" ;;
    x86_64-linux-android) assets_abi="x86_64"; jni_abi="x86_64" ;;
    armv7a-linux-android) assets_abi="armeabi-v7a"; jni_abi="armeabi-v7a" ;;
    *) echo "Unsupported Android host: $host" >&2; exit 1 ;;
  esac

  if [[ ! -f "$toolchain" ]]; then
    log "Building Android depends for $host..."
    (
      cd "$REPO_ROOT/depends"
      env -u CC -u CXX -u CPP -u LD -u AR -u RANLIB -u STRIP \
        NO_QT=1 \
        NO_UPNP=1 \
        NO_NATPMP=1 \
        NO_ZMQ=1 \
        NO_IPC=1 \
        ANDROID_SDK="$ANDROID_SDK" \
        ANDROID_NDK="$ANDROID_NDK" \
        ANDROID_API_LEVEL="$ANDROID_API_LEVEL" \
        ANDROID_TOOLCHAIN_BIN="$ANDROID_TOOLCHAIN_BIN" \
        make HOST="$host" -j"$JOBS"
    )
  else
    log "Reusing Android depends for $host ($toolchain)"
  fi

  if [[ ! -f "$toolchain" ]]; then
    echo "Depends toolchain missing after build: $toolchain" >&2
    exit 1
  fi

  log "CMake configure for $host"
  /bin/rm -rf "$build_dir"
  # Regenerate toolchain after android.mk changes (packages already cached).
  (
    cd "$REPO_ROOT/depends"
    env -u CC -u CXX -u CPP -u LD -u AR -u RANLIB -u STRIP \
      NO_QT=1 NO_UPNP=1 NO_NATPMP=1 NO_ZMQ=1 NO_IPC=1 \
      ANDROID_SDK="$ANDROID_SDK" \
      ANDROID_NDK="$ANDROID_NDK" \
      ANDROID_API_LEVEL="$ANDROID_API_LEVEL" \
      ANDROID_TOOLCHAIN_BIN="$ANDROID_TOOLCHAIN_BIN" \
      make HOST="$host" -j"$JOBS"
  )
  cmake -S "$REPO_ROOT" -B "$build_dir" \
    --toolchain "$toolchain" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_DAEMON=ON \
    -DBUILD_CLI=ON \
    -DBUILD_GUI=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCH=OFF \
    -DBUILD_FUZZ_BINARY=OFF \
    -DBUILD_TX=OFF \
    -DBUILD_UTIL=OFF \
    -DBUILD_WALLET=ON \
    -DBUILD_WALLET_TOOL=OFF \
    -DWITH_ZMQ=OFF \
    -DENABLE_IPC=OFF \
    -DCMAKE_CXX_COMPILER_WORKS=1 \
    -DCMAKE_C_COMPILER_WORKS=1

  log "CMake build hobbyhashd for $host"
  cmake --build "$build_dir" --parallel "$JOBS" --target hobbyhashd

  local binary
  binary="$(find "$build_dir" -type f -name hobbyhashd | head -1)"
  if [[ -z "$binary" ]]; then
    echo "hobbyhashd not found in $build_dir" >&2
    exit 1
  fi

  local asset_dir="$ANDROID_WALLET_ROOT/app/src/main/assets/bin/$assets_abi"
  local jni_dir="$ANDROID_WALLET_ROOT/app/src/main/jniLibs/$jni_abi"
  mkdir -p "$asset_dir" "$jni_dir"
  install -m 0755 "$binary" "$asset_dir/hobbyhashd"
  # Android loads native libs as lib*.so from jniLibs.
  install -m 0755 "$binary" "$jni_dir/libhobbyhashd.so"
  # hobbyhashd links libc++_shared.so from the NDK — ship it beside the node.
  local ndk_triple=""
  case "$host" in
    aarch64-linux-android) ndk_triple="aarch64-linux-android" ;;
    x86_64-linux-android) ndk_triple="x86_64-linux-android" ;;
    armv7a-linux-android) ndk_triple="arm-linux-androideabi" ;;
  esac
  local cxx_shared="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/$ndk_triple/libc++_shared.so"
  if [[ ! -f "$cxx_shared" ]]; then
    echo "libc++_shared.so not found: $cxx_shared" >&2
    exit 1
  fi
  install -m 0644 "$cxx_shared" "$jni_dir/libc++_shared.so"

  file "$asset_dir/hobbyhashd"
  "$ANDROID_TOOLCHAIN_BIN/llvm-readelf" -d "$asset_dir/hobbyhashd" || true
  strings "$asset_dir/hobbyhashd" | rg -m1 'v31\.|Satoshi:|HobbyHash Core version' || true
  log "Packaged $asset_dir/hobbyhashd and $jni_dir/libhobbyhashd.so (+ libc++_shared.so)"
}

log "Source: $REPO_ROOT"
log "Wallet: $ANDROID_WALLET_ROOT"
log "NDK: $ANDROID_NDK"
log "Hosts: ${HOSTS[*]}"

for host in ${HOSTS[@]}; do
  build_host "$host"
done

log "Android node build complete."
