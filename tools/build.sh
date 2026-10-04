#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
# Never initialize compiler Wine state in the user's game/default prefix.
export WINEPREFIX="${NEURO_FNV_BUILD_WINEPREFIX:-$ROOT_DIR/.cloud-tools/wine}"

usage() {
  cat <<'EOF'
Usage: bash tools/build.sh [Debug|Release] [--check] [--clean-first]

Configures/builds the 32-bit Windows plugin with the real msvc-wine x86 cl,
rc, and link wrappers. Uses build/cloud by default and never deletes build
outputs. Set NEURO_FNV_BUILD_DIR or NEURO_FNV_MSVC_ROOT to override paths.
--check verifies toolchain prerequisites without configuring or compiling.
EOF
}
configuration=Debug
check_only=0
clean_first=0
for arg in "$@"; do
  case "$arg" in
    Debug|Release) configuration="$arg" ;;
    --check) check_only=1 ;;
    --clean-first) clean_first=1 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done
build_dir="${NEURO_FNV_BUILD_DIR:-$ROOT_DIR/build/cloud}"
msvc_root="${NEURO_FNV_MSVC_ROOT:-/opt/msvc}"
for tool in cl rc link mt; do
  if [[ ! -x "$msvc_root/bin/x86/$tool" ]]; then
    echo "Missing x86 MSVC wrapper: $msvc_root/bin/x86/$tool (set NEURO_FNV_MSVC_ROOT or opt in to tools/setup-msvc.sh)." >&2
    exit 1
  fi
done
if [[ ! -d "$msvc_root/cmake/find_root/x86" ]]; then
  echo "Missing x86 Windows SDK find root: $msvc_root/cmake/find_root/x86." >&2
  exit 1
fi
if ! command -v wine64 >/dev/null && ! command -v wine >/dev/null; then
  echo "Missing build prerequisite: Wine launcher (wine or wine64)." >&2
  exit 1
fi
cmake_bin="$ROOT_DIR/.cloud-tools/bin/cmake"
if [[ ! -x "$cmake_bin" ]]; then
  cmake_bin="$(command -v cmake || true)"
fi
if [[ -z "$cmake_bin" ]]; then
  echo "Missing build prerequisite: cmake (run tools/cloud-setup.sh)." >&2
  exit 1
fi
command -v ninja >/dev/null || { echo "Missing build prerequisite: ninja (run tools/cloud-setup.sh)." >&2; exit 1; }
if ! "$cmake_bin" --version | python3 -c 'import re,sys; s=sys.stdin.read(); m=re.search(r"cmake version ([0-9]+)[.]([0-9]+)", s); sys.exit(0 if m and tuple(map(int,m.groups())) >= (3,30) else 1)'; then
  echo "CMake >= 3.30 is required." >&2
  exit 1
fi
if [[ ! -f "$ROOT_DIR/libs/libneurosdk/CMakeLists.txt" ]]; then
  echo "Required libneurosdk submodule is not initialized; run bash tools/cloud-setup.sh." >&2
  exit 1
fi
if (( check_only )); then
  printf 'Ready: CMake, Ninja, x86 cl/rc/link wrappers and Windows SDK at %s (build dir %s).\n' "$msvc_root" "$build_dir"
  exit 0
fi
mkdir -p "$(dirname "$WINEPREFIX")"
"$cmake_bin" -S "$ROOT_DIR" -B "$build_dir" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/windows-i686-msvc.cmake" \
  -DNEURO_FNV_MSVC_ROOT="$msvc_root" \
  -DCMAKE_BUILD_TYPE="$configuration" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
build_options=()
if (( clean_first )); then build_options+=(--clean-first); fi
"$cmake_bin" --build "$build_dir" --parallel "${build_options[@]}"
