#!/usr/bin/env bash
# V4 fork tests for Windows cross-build: unit tests via mingw + wine, PE sanity on exes.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$REPO_ROOT/src"
ROOT="$(cd "$REPO_ROOT/../.." && pwd)"
BUILD="${V4_BUILD_MINGW:-$ROOT/build-mingw-x86_64}"
DIST="${V4_DIST_WIN:-$ROOT/dist/win64}"
HOST=x86_64-w64-mingw32
CXX="${HOST}-g++"
FAILED=0

log() { echo "==> $*"; }
fail() { echo "FAIL: $*" >&2; FAILED=1; }

echo "=== V4 Windows fork tests (mingw cross) ==="
echo "Host: $(. /etc/os-release && echo "$PRETTY_NAME")"

if [[ ! -x "$DIST/hobbyhashd.exe" ]]; then
  log "Windows binaries missing — running build-v4-mingw.sh"
  bash "$SCRIPT_DIR/build-v4-mingw.sh"
fi

log "PE sanity on V4 Windows binaries"
for exe in hobbyhashd.exe hobbyhash-cli.exe hobbyhash-wallet.exe; do
  path="$DIST/$exe"
  if [[ ! -f "$path" ]]; then
    fail "missing $path"
    continue
  fi
  if file "$path" | grep -q 'PE32+ executable'; then
    echo "$exe: PE32+ OK ($(stat -c%s "$path") bytes)"
  else
    fail "$exe is not a PE32+ executable"
  fi
done

log "Fork markers in hobbyhashd.exe (strings)"
if grep -qi 'HobbyHash' < <(strings "$DIST/hobbyhashd.exe" 2>/dev/null); then
  echo "hobbyhashd.exe contains HobbyHash branding: PASS"
else
  fail "hobbyhashd.exe missing HobbyHash strings"
fi

MINGW_CXXFLAGS=(
  -std=c++20
  -DWIN32
  -D_WIN32
  -I"$SRC"
  -I"$BUILD/src"
  -include "$BUILD/src/bitcoin-build-config.h"
  -DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION
  -DBOOST_NO_CXX98_FUNCTION_BASE
  -static
  -O2
)

run_mingw_test() {
  local name=$1
  shift
  local out="/tmp/${name}.exe"
  log "Cross-compile: $name"
  if ! (cd "$SRC" && "$CXX" "${MINGW_CXXFLAGS[@]}" "$@" -o "$out") 2>"/tmp/${name}_build.log"; then
    fail "$name compile failed (see /tmp/${name}_build.log)"
    tail -20 "/tmp/${name}_build.log" >&2
    return
  fi
  if command -v wine >/dev/null 2>&1; then
    if wine "$out" 2>/dev/null; then
      echo "$name (wine): PASS"
    else
      fail "$name wine execution failed"
    fi
  else
    if file "$out" | grep -q 'PE32+ executable'; then
      echo "$name: PE32+ built OK (wine not installed — skipped runtime)"
    else
      fail "$name output is not PE32+"
    fi
  fi
}

run_mingw_test v4_fork_subsidy_tests_mingw \
  test/v4_fork_subsidy_tests.cpp \
  consensus/hashrate_subsidy.cpp \
  arith_uint256.cpp \
  uint256.cpp \
  crypto/hex_base.cpp \
  util/strencodings.cpp \
  util/string.cpp

log "Cross-compile: v4_fork_replay_tests_mingw (link V4 mingw libs)"
LIBDIRS=(
  "$BUILD/lib"
  "$BUILD/src"
  "$BUILD/src/secp256k1/lib"
  "$BUILD/src/univalue"
)
LIBPATHS=()
for d in "${LIBDIRS[@]}"; do
  [[ -d "$d" ]] && LIBPATHS+=("-L$d")
done
REPLAY_OUT="/tmp/v4_fork_replay_tests_mingw.exe"
if ! (cd "$SRC" && "$CXX" "${MINGW_CXXFLAGS[@]}" "${LIBPATHS[@]}" \
  test/v4_fork_replay_tests.cpp \
  -lbitcoin_common -lbitcoin_consensus -lbitcoin_util -lbitcoin_crypto -lbitcoin_clientversion \
  -lunivalue -lsecp256k1 -lleveldb -lcrc32c -lminisketch \
  -lbcrypt \
  -static \
  -o "$REPLAY_OUT") 2>/tmp/v4_fork_replay_tests_mingw_build.log; then
  fail "v4_fork_replay_tests_mingw compile failed (see /tmp/v4_fork_replay_tests_mingw_build.log)"
  tail -20 /tmp/v4_fork_replay_tests_mingw_build.log >&2
elif command -v wine >/dev/null 2>&1; then
  if wine "$REPLAY_OUT" 2>/dev/null; then
    echo "v4_fork_replay_tests_mingw (wine): PASS"
  else
    fail "v4_fork_replay_tests_mingw wine execution failed"
  fi
elif file "$REPLAY_OUT" | grep -q 'PE32+ executable'; then
  echo "v4_fork_replay_tests_mingw: PE32+ built OK (wine not installed — skipped runtime)"
else
  fail "v4_fork_replay_tests_mingw output is not PE32+"
fi

if command -v wine >/dev/null 2>&1; then
  log "hobbyhash-cli.exe --version (wine)"
  if wine "$DIST/hobbyhash-cli.exe" --version 2>/dev/null | grep -qi hobbyhash; then
    echo "hobbyhash-cli --version: PASS"
  else
    fail "hobbyhash-cli --version failed under wine"
  fi
else
  echo "wine not installed — skipped hobbyhash-cli runtime smoke test"
fi

echo ""
if [[ "$FAILED" -eq 0 ]]; then
  echo "=== ALL V4 WINDOWS FORK TESTS PASSED ==="
  exit 0
fi
echo "=== V4 WINDOWS FORK TESTS FAILED ==="
exit 1
