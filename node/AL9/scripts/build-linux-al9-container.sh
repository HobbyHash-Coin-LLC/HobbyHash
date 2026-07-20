#!/usr/bin/env bash
# Build AlmaLinux 9 / RHEL 9 compatibility node package (V4 BC 31, CMake in AL9 container).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
IMAGE_NAME="hobbyhash-al9-builder"
CONTAINERFILE="$REPO_ROOT/packaging/al9/Containerfile"
PACKAGE_NAME="HobbyHash-Linux-Node-AL9-x86_64"
TARBALL_PATH="$REPO_ROOT/dist/$PACKAGE_NAME.tar.gz"

if [[ "$(pwd -P)" != "$REPO_ROOT" ]]; then
  echo "Run this script from the source repo root: $REPO_ROOT" >&2
  exit 1
fi

if ! command -v podman >/dev/null 2>&1; then
  echo "podman is required to build the AL9 container release." >&2
  exit 1
fi

if [[ ! -f "$CONTAINERFILE" ]]; then
  echo "Missing container definition: $CONTAINERFILE" >&2
  exit 1
fi

echo "Building AL9 builder image: $IMAGE_NAME"
podman build \
  --tag "$IMAGE_NAME" \
  --file "$CONTAINERFILE" \
  "$REPO_ROOT/packaging/al9"

echo "Running AL9 CMake build inside container."
podman run --rm \
  --userns=keep-id \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp \
  --env JOBS="${JOBS:-}" \
  --security-opt label=disable \
  --volume "$REPO_ROOT:/workspace:rw" \
  "$IMAGE_NAME" \
  bash -lc '
set -euo pipefail

source /etc/os-release
if [[ "${ID:-}" != "almalinux" || "${VERSION_ID:-}" != 9* ]]; then
  echo "This build must run inside an AlmaLinux 9 container." >&2
  exit 1
fi

REPO_ROOT="/workspace"
BUILD_DIR="$REPO_ROOT/build-al9-container-x86_64"
BUILD_SRC_DIR="$BUILD_DIR/source"
CMAKE_BUILD_DIR="$BUILD_DIR/cmake"
RELEASE_DIR="$REPO_ROOT/release-al9-container-x86_64"
PACKAGE_NAME="HobbyHash-Linux-Node-AL9-x86_64"
PACKAGE_DIR="$RELEASE_DIR/$PACKAGE_NAME"
DIST_DIR="$REPO_ROOT/dist"
TARBALL_PATH="$DIST_DIR/$PACKAGE_NAME.tar.gz"

BINARIES=(
  hobbyhashd
  hobbyhash-cli
  hobbyhash-wallet
  hobbyhash-tx
  hobbyhash-util
)

CMAKE_TARGETS=(
  hobbyhashd
  hobbyhash-cli
  hobbyhash-wallet
  hobbyhash-tx
  hobbyhash-util
)

JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
JOBS="${JOBS:-2}"

cd "$REPO_ROOT"

mkdir -p "$BUILD_DIR" "$RELEASE_DIR" "$DIST_DIR"
rm -rf "$BUILD_SRC_DIR" "$CMAKE_BUILD_DIR" "$PACKAGE_DIR"
mkdir -p "$BUILD_SRC_DIR" "$CMAKE_BUILD_DIR" "$PACKAGE_DIR"

tar \
  --exclude="./build-al9-x86_64" \
  --exclude="./build-al9-container-x86_64" \
  --exclude="./build-linux-x86_64" \
  --exclude="./build-mingw-x86_64" \
  --exclude="./release-al9-x86_64" \
  --exclude="./release-al9-container-x86_64" \
  --exclude="./release-linux-x86_64" \
  --exclude="./dist" \
  --exclude="./.git" \
  -C "$REPO_ROOT" \
  -cf - . | tar -C "$BUILD_SRC_DIR" -xf -

cmake \
  -S "$BUILD_SRC_DIR" \
  -B "$CMAKE_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="-O2 -march=x86-64-v2" \
  -DCMAKE_CXX_FLAGS="-O2 -march=x86-64-v2" \
  -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
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

cmake --build "$CMAKE_BUILD_DIR" --parallel "$JOBS" --target "${CMAKE_TARGETS[@]}"

find_built_binary() {
  local binary="$1"
  local candidate
  for candidate in \
    "$CMAKE_BUILD_DIR/src/$binary" \
    "$CMAKE_BUILD_DIR/$binary" \
    "$BUILD_SRC_DIR/src/$binary"; do
    if [[ -f "$candidate" ]]; then
      printf "%s\n" "$candidate"
      return 0
    fi
  done
  candidate="$(find "$CMAKE_BUILD_DIR" -type f -name "$binary" -perm -111 2>/dev/null | head -1 || true)"
  if [[ -n "$candidate" && -f "$candidate" ]]; then
    printf "%s\n" "$candidate"
    return 0
  fi
  return 1
}

for binary in "${BINARIES[@]}"; do
  built_path="$(find_built_binary "$binary")" || {
    echo "Required binary was not built: $binary" >&2
    exit 1
  }
  install -m 0755 "$built_path" "$PACKAGE_DIR/$binary"
done

tar -C "$RELEASE_DIR" -czf "$TARBALL_PATH" "$PACKAGE_NAME"

echo
echo "Version checks:"
"$PACKAGE_DIR/hobbyhashd" --version
"$PACKAGE_DIR/hobbyhash-cli" --version

check_ldd() {
  local binary="$1"
  local output

  echo
  echo "ldd $binary:"
  output="$(ldd "$binary" 2>&1 || true)"
  printf "%s\n" "$output"
  if grep -q "not found" <<<"$output"; then
    echo "Missing shared library detected for $binary" >&2
    exit 1
  fi
}

check_ldd "$PACKAGE_DIR/hobbyhashd"
check_ldd "$PACKAGE_DIR/hobbyhash-cli"

echo
echo "Tarball contents:"
tar -tzf "$TARBALL_PATH"
echo
echo "Created: $TARBALL_PATH"
'

if [[ ! -f "$TARBALL_PATH" ]]; then
  echo "Container build did not create $TARBALL_PATH" >&2
  exit 1
fi

echo "AL9 container release package is ready: $TARBALL_PATH"
