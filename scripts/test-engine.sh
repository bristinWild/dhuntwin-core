#!/usr/bin/env bash
# Build dhun_engine and run its unit tests (incl. the ported upstream
# suites) and the parity tests against tests/golden.
# Needs cmake, a C++20 compiler and nlohmann_json. With Nix available
# (unless DHUN_NO_NIX=1), the toolchain comes from nixpkgs.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
build="${DHUN_BUILD_DIR:-$here/build/engine}"

run() {
  cmake -S "$here/lib/dhun_engine" -B "$build" -DDHUN_ENGINE_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
  cmake --build "$build" --parallel
  "$build/dhun_unit_test"
  "$build/dhun_parity_test" "$here/tests/golden"
}

if [[ -z "${DHUN_NO_NIX:-}" ]] && command -v nix >/dev/null 2>&1; then
  export -f run; export here build
  nix shell nixpkgs#cmake nixpkgs#gcc nixpkgs#nlohmann_json nixpkgs#gnumake -c bash -c run
else
  run
fi
