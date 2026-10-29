#!/usr/bin/env bash
# Drive the module through its generated C ABI the way logos_host does,
# without Nix: derive the LIDL from the impl header, generate the C-ABI
# wrapper with logos-cpp-generator, link it with the engine and run
# tests/host/host_smoke.cpp.
#   LOGOS_CPP_GENERATOR=/path/to/logos-cpp-generator \
#   LOGOS_CPP_SDK=/path/to/logos-cpp-sdk LOGOS_PROTOCOL=/path/to/logos-protocol \
#   scripts/test-host.sh
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
gen="${LOGOS_CPP_GENERATOR:?set LOGOS_CPP_GENERATOR}"
sdk="${LOGOS_CPP_SDK:?set LOGOS_CPP_SDK (logos-cpp-sdk checkout)}"
proto="${LOGOS_PROTOCOL:?set LOGOS_PROTOCOL (logos-protocol checkout)}"
out="$here/build/host"
engine="$here/build/engine"
rm -rf "$out" && mkdir -p "$out/gen"

cmake -S "$here/lib/dhun_engine" -B "$engine" -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build "$engine" --parallel >/dev/null

"$gen" --header-to-lidl "$here/src/dhuntwin_core_impl.h" --impl-class DhuntwinCoreImpl \
  --metadata "$here/metadata.json" -o "$out/gen/dhuntwin_core.lidl"
"$gen" --lidl "$out/gen/dhuntwin_core.lidl" --backend cdylib --impl-class DhuntwinCoreImpl \
  --impl-header dhuntwin_core_impl.h --output-dir "$out/gen"
"$gen" --metadata "$here/metadata.json" --umbrella --api-style lp --output-dir "$out/gen"

inc=(-I"$out/gen" -I"$here/src" -I"$here/lib/dhun_engine/include" -I"$sdk/cpp" -I"$proto/cpp")
objs=()
for f in "$out"/gen/dhuntwin_core_module_impl.cpp "$out"/gen/dhuntwin_core_events_cdylib.cpp \
         "$out"/gen/logos_sdk.cpp "$here/src/dhuntwin_core_impl.cpp" "$here/tests/host/lp_stubs.cpp"; do
  o="$out/$(basename "$f").o"
  c++ -std=c++20 -fPIC -c "${inc[@]}" "$f" -o "$o"
  objs+=("$o")
done
c++ -shared -o "$out/libdhuntwin_core_impl.so" "${objs[@]}" "$engine/libdhun_engine.a" -Wl,--no-undefined
c++ -std=c++20 "$here/tests/host/host_smoke.cpp" -o "$out/host_smoke" -ldl
"$out/host_smoke" "$out/libdhuntwin_core_impl.so"
