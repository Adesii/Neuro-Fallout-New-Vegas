#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export WINEPREFIX="${NEURO_FNV_BUILD_WINEPREFIX:-$ROOT_DIR/.cloud-tools/wine}"
MSVC_ROOT="${NEURO_FNV_MSVC_ROOT:-/opt/msvc}"
MSVC_WINE_REV="514f8ea34842cd6d831804d0e9658d3a32870ae1"

usage() {
  cat <<'EOF'
Usage: NEURO_FNV_ACCEPT_MSVC_LICENSE=1 bash tools/setup-msvc.sh

Opt-in only: downloads Microsoft's non-redistributable MSVC/Windows SDK files
using mstorsjo/msvc-wine pinned to a reviewed source revision. Read and accept
Microsoft's license terms yourself before setting the acknowledgment variable.
Optional override: NEURO_FNV_MSVC_ROOT (default /opt/msvc).
EOF
}

case "${1:-}" in
  -h|--help) usage; exit 0 ;;
  "") ;;
  *) usage >&2; exit 2 ;;
esac
if [[ "${NEURO_FNV_ACCEPT_MSVC_LICENSE:-}" != 1 ]]; then
  echo "Refusing MSVC provisioning: review Microsoft's license and explicitly set NEURO_FNV_ACCEPT_MSVC_LICENSE=1." >&2
  exit 2
fi
if [[ -x "$MSVC_ROOT/bin/x86/cl" && -x "$MSVC_ROOT/bin/x86/rc" && -x "$MSVC_ROOT/bin/x86/link" && -d "$MSVC_ROOT/cmake/find_root/x86" ]]; then
  echo "Existing x86 MSVC toolchain found at $MSVC_ROOT; leaving it unchanged."
  exit 0
fi
for command in git python3; do
  command -v "$command" >/dev/null || { echo "Missing prerequisite: $command (run tools/cloud-setup.sh --with-msvc)." >&2; exit 1; }
done
if ! command -v wine64 >/dev/null && ! command -v wine >/dev/null; then
  echo "Missing Wine launcher: install wine/wine64 so wine or wine64 is on PATH." >&2
  exit 1
fi
WORK_DIR="$ROOT_DIR/.cloud-tools/msvc-wine-$MSVC_WINE_REV"
mkdir -p "$(dirname "$WORK_DIR")"
if [[ ! -d "$WORK_DIR/.git" ]]; then
  git clone https://github.com/mstorsjo/msvc-wine.git "$WORK_DIR"
fi
git -C "$WORK_DIR" fetch --depth 1 origin "$MSVC_WINE_REV"
git -C "$WORK_DIR" checkout --detach "$MSVC_WINE_REV"
python3 "$WORK_DIR/vsdownload.py" --accept-license --architecture x86 --msvc-version 18.0 --sdk-version 10.0.26100 --dest "$MSVC_ROOT"
"$WORK_DIR/install.sh" "$MSVC_ROOT"
for tool in cl rc link; do
  [[ -x "$MSVC_ROOT/bin/x86/$tool" ]] || { echo "Provisioning did not produce required x86 wrapper $MSVC_ROOT/bin/x86/$tool." >&2; exit 1; }
done
[[ -d "$MSVC_ROOT/cmake/find_root/x86" ]] || { echo "Provisioning did not produce the x86 CMake find root." >&2; exit 1; }
echo "Installed x86 MSVC/Windows SDK under $MSVC_ROOT. This is not a build-success claim."
