#!/usr/bin/env bash
# Build standard Linux / AlmaLinux 10 node package (V4 BC 31, CMake).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build-linux-x86_64"
RELEASE_DIR="$REPO_ROOT/release-linux-x86_64"
PACKAGE_NAME="HobbyHash-Linux-Node-x86_64"
PACKAGE_DIR="$RELEASE_DIR/$PACKAGE_NAME"
DIST_DIR="$REPO_ROOT/dist"
TARBALL_PATH="$DIST_DIR/$PACKAGE_NAME.tar.gz"

BINARIES=(
  hobbyhashd
  hobbyhash-cli
)

CMAKE_TARGETS=(
  hobbyhashd
  hobbyhash-cli
)

JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

if [[ "$(pwd -P)" != "$REPO_ROOT" ]]; then
  echo "Run from the source repo root: $REPO_ROOT" >&2
  exit 1
fi

mkdir -p "$BUILD_DIR" "$RELEASE_DIR" "$DIST_DIR"
rm -rf "$PACKAGE_DIR"
mkdir -p "$PACKAGE_DIR"

cmake \
  -S "$REPO_ROOT" \
  -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_DAEMON=ON \
  -DBUILD_CLI=ON \
  -DBUILD_GUI=OFF \
  -DBUILD_TESTS=OFF \
  -DBUILD_BENCH=OFF \
  -DBUILD_FUZZ_BINARY=OFF \
  -DBUILD_TX=ON \
  -DBUILD_UTIL=ON \
  -DBUILD_WALLET=ON \
  -DWITH_ZMQ=OFF \
  -DENABLE_IPC=OFF

cmake --build "$BUILD_DIR" --parallel "$JOBS" --target "${CMAKE_TARGETS[@]}"

find_built_binary() {
  local binary="$1"
  local candidate
  for candidate in \
    "$BUILD_DIR/src/$binary" \
    "$BUILD_DIR/$binary"; do
    if [[ -f "$candidate" ]]; then
      printf "%s\n" "$candidate"
      return 0
    fi
  done
  candidate="$(find "$BUILD_DIR" -type f -name "$binary" -perm -111 2>/dev/null | head -1 || true)"
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
echo
echo "Created: $TARBALL_PATH"
