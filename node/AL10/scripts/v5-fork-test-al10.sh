#!/usr/bin/env bash
# V5 dual-PoW native unit tests (AL10 x86_64).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="$REPO_ROOT/src"
ROOT="$(cd "$REPO_ROOT/../.." && pwd)"
OUT="/tmp/v5_dual_pow_tests"

log() { echo "==> $*"; }

CXXFLAGS=(
  -std=c++20
  -I"$SRC"
  -O2
)

SOURCES=(
  "$SRC/test/v5_dual_pow_tests.cpp"
  "$SRC/consensus/dual_pow.cpp"
  "$SRC/consensus/hashrate_subsidy.cpp"
  "$SRC/arith_uint256.cpp"
  "$SRC/uint256.cpp"
  "$SRC/crypto/hex_base.cpp"
  "$SRC/util/strencodings.cpp"
  "$SRC/util/string.cpp"
)

log "Compile v5_dual_pow_tests"
(cd "$SRC" && g++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$OUT")

log "Run v5_dual_pow_tests"
"$OUT"

log "V5 native unit tests: PASS"
