#!/usr/bin/env bash
# Builds and runs the host-side unit tests (tests/). These cover engine-independent logic only; they say nothing
# about in-game behaviour. Usage: bash tools/test.sh [name-filter]
set -euo pipefail
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${NEURO_FNV_TEST_BUILD_DIR:-$ROOT_DIR/build/tests}"
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

# Build output is only shown when a step fails, so a compile error is never swallowed.
quiet() {
  if ! "$@" >"$log" 2>&1; then
    cat "$log" >&2
    exit 1
  fi
}

quiet cmake -S "$ROOT_DIR/tests" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug
# A source in an engine-free directory that the host tests do not compile is only a warning, but never a silent one.
if grep -A2 "CMake Warning" "$log" >&2; then
  echo "WARNING: add the source above to tests/CMakeLists.txt, or move it out of an engine-free directory." >&2
fi
quiet cmake --build "$build_dir"
"$build_dir/neuro_fnv_tests" "$@"
