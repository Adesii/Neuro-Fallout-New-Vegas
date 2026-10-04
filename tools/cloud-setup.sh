#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="$ROOT_DIR/.cloud-tools/bin:$PATH"
cd "$ROOT_DIR"

usage() {
  cat <<'EOF'
Usage: bash tools/cloud-setup.sh [--with-msvc] [--check]

Installs base source-development tools, initializes the required SDK submodule,
and acquires/checks public references. No compilation is performed. Optional
MSVC provisioning downloads Microsoft's non-redistributable compiler only when
--with-msvc and NEURO_FNV_ACCEPT_MSVC_LICENSE=1 are both supplied.
EOF
}
with_msvc=0
check_only=0
for arg in "$@"; do
  case "$arg" in
    --with-msvc) with_msvc=1 ;;
    --check) check_only=1 ;;
    -h|--help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

if (( check_only )); then
  failures=0
  for tool in git python3 clang-format rg ninja; do
    if command -v "$tool" >/dev/null 2>&1; then
      printf 'Tool %-14s %s\n' "$tool" "$("$tool" --version 2>&1 | python3 -c 'import sys; print(sys.stdin.read().splitlines()[0])')"
    else
      printf 'Tool %-14s MISSING\n' "$tool"
      failures=1
    fi
  done
  cmake_bin="$ROOT_DIR/.cloud-tools/bin/cmake"
  [[ -x "$cmake_bin" ]] || cmake_bin="$(command -v cmake || true)"
  if [[ -n "$cmake_bin" ]]; then
    cmake_version="$("$cmake_bin" --version | python3 -c 'import sys; print(sys.stdin.readline().strip())')"
    printf 'Tool %-14s %s\n' cmake "$cmake_version"
    if ! "$cmake_bin" --version | python3 -c 'import re,sys; m=re.search(r"cmake version ([0-9]+)[.]([0-9]+)",sys.stdin.read()); sys.exit(0 if m and tuple(map(int,m.groups())) >= (3,30) else 1)'; then failures=1; fi
  else
    echo 'Tool cmake          MISSING'
    failures=1
  fi
  expected_sdk="$(git ls-tree HEAD libs/libneurosdk | python3 -c 'import sys; parts=sys.stdin.read().split(); print(parts[2] if len(parts) > 2 else "")')"
  if [[ -f libs/libneurosdk/CMakeLists.txt ]]; then
    actual_sdk="$(git -C libs/libneurosdk rev-parse HEAD)"
    sdk_dirty=no
    [[ -z "$(git -C libs/libneurosdk status --porcelain)" ]] || sdk_dirty=yes
    if [[ "$actual_sdk" == "$expected_sdk" ]]; then sdk_state=pinned; else sdk_state=unpinned; failures=1; fi
    printf 'SDK libneurosdk      %s (%s; dirty=%s; expected=%s)\n' "$actual_sdk" "$sdk_state" "$sdk_dirty" "$expected_sdk"
  else
    echo 'SDK libneurosdk      MISSING (submodule not initialized)'
    failures=1
  fi
  if python3 tools/references.py check; then :; else failures=1; fi
  msvc_root="${NEURO_FNV_MSVC_ROOT:-/opt/msvc}"
  if [[ -x "$msvc_root/bin/x86/cl" && -x "$msvc_root/bin/x86/rc" && -x "$msvc_root/bin/x86/link" && -d "$msvc_root/cmake/find_root/x86" ]]; then
    printf 'MSVC x86             installed (%s)\n' "$msvc_root"
  else
    printf 'MSVC x86             missing (optional; %s)\n' "$msvc_root"
  fi
  exit "$failures"
fi

if [[ "$(id -u)" -eq 0 ]]; then
  apt=(apt-get)
elif command -v sudo >/dev/null 2>&1; then
  apt=(sudo apt-get)
else
  echo "Install base packages as root (git, python3, clang-format, ripgrep, cmake >= 3.30, ninja-build)." >&2
  exit 1
fi
"${apt[@]}" update
"${apt[@]}" install -y git python3 python3-pip clang-format ripgrep ninja-build
if [[ ! -x "$ROOT_DIR/.cloud-tools/bin/cmake" ]] && { ! command -v cmake >/dev/null 2>&1 || ! cmake --version | python3 -c 'import re,sys; s=sys.stdin.read(); m=re.search(r"cmake version (\d+)\.(\d+)", s); sys.exit(0 if m and tuple(map(int,m.groups())) >= (3,30) else 1)'; }; then
  "${apt[@]}" install -y python3-venv
  venv="$ROOT_DIR/.cloud-tools/cmake-venv"
  python3 -m venv "$venv"
  "$venv/bin/python" -m pip install --upgrade 'cmake>=3.30'
  mkdir -p "$ROOT_DIR/.cloud-tools/bin"
  ln -sfn "$venv/bin/cmake" "$ROOT_DIR/.cloud-tools/bin/cmake"
  export PATH="$ROOT_DIR/.cloud-tools/bin:$PATH"
fi

cmake --version
if ! git submodule status -- libs/libneurosdk | grep -q '^ '; then
  git submodule update --init --recursive -- libs/libneurosdk
fi
python3 tools/references.py setup
python3 tools/references.py check

if (( with_msvc )); then
  if [[ "${NEURO_FNV_ACCEPT_MSVC_LICENSE:-}" != 1 ]]; then
    echo "MSVC is optional and was not provisioned: review Microsoft's license, then rerun with --with-msvc and NEURO_FNV_ACCEPT_MSVC_LICENSE=1." >&2
    exit 2
  fi
  if ! "${apt[@]}" install -y wine wine64 python3 msitools libgcab-1.0-0 ca-certificates winbind; then
    echo "Optional MSVC prerequisites could not be installed; public reference/source setup remains available. No compile was attempted." >&2
    exit 1
  fi
  if ! bash tools/setup-msvc.sh; then
    echo "Optional MSVC provisioning failed; public reference/source setup remains available. No compile was attempted." >&2
    exit 1
  fi
fi
