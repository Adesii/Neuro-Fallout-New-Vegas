#!/usr/bin/env bash
# Agent-neutral environment setup for Claude Code cloud, Codex Cloud and local checkouts. See docs/cloud-development.md.
set -uo pipefail
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="$ROOT_DIR/.cloud-tools/bin:$PATH"
cd "$ROOT_DIR" || exit 1
# shellcheck source=tools/lib/env.sh
source "$ROOT_DIR/tools/lib/env.sh"

usage() {
  cat <<'EOF'
Usage: bash tools/cloud-setup.sh [--with-msvc] [--check] [--exit-zero]

Installs only the missing base tools, initializes the SDK submodule and acquires the pinned public references
(shallow, in parallel). No compilation is performed. Each phase prints its duration; the total is compared with the
~5 minute budget Claude Code cloud allows for caching the environment.

Exit status: 0 ready, 3 degraded (something optional or network-dependent is unavailable; source and the other
pieces stay usable), 1 a required step failed, 2 usage error. --exit-zero (or NEURO_FNV_SETUP_SOFT=1) prints the same
summary but always exits 0: Claude Code cloud fails session start on a non-zero setup script.

--check is fully offline and fast: it reports the same status without installing or fetching anything.

Optional MSVC provisioning downloads Microsoft's non-redistributable compiler only when --with-msvc and
NEURO_FNV_ACCEPT_MSVC_LICENSE=1 are both supplied.

Environment: NEURO_FNV_CACHE_DIR (default /opt/neuro-fnv-cache as root, else .cloud-tools/cache) holds references and
an SDK mirror outside the checkout; NEURO_FNV_REF_JOBS (default 9), NEURO_FNV_GIT_TIMEOUT (default 60 s per git command) tune fetching.
EOF
}

with_msvc=0
check_only=0
soft="${NEURO_FNV_SETUP_SOFT:-0}"
for arg in "$@"; do
  case "$arg" in
    --with-msvc) with_msvc=1 ;;
    --check) check_only=1 ;;
    --exit-zero) soft=1 ;;
    -h | --help) usage; exit 0 ;;
    *) usage >&2; exit 2 ;;
  esac
done

finish() {
  local rc=0
  summary_status || rc=$?
  if [[ "$soft" == 1 && "$rc" -ne 0 ]]; then
    echo "(--exit-zero: exiting 0 despite status code $rc)"
    rc=0
  fi
  exit "$rc"
}

if ((check_only)); then
  readiness_report
  finish
fi

total_t0=$(now_us)
cache=$(resolve_cache_dir)

# --- tools -------------------------------------------------------------------------------------------------------
phase_start tools
if ! install_missing_tools; then
  mapfile -t still_missing < <(missing_tools "${REQUIRED_TOOLS[@]}" "${OPTIONAL_TOOLS[@]}")
  ((${#still_missing[@]})) && echo "  note: could not install base tools (no root/sudo, or apt failed): ${still_missing[*]}"
fi
phase_end

# --- cmake >= 3.30 for the plugin build (host tests need only 3.20); kept in the cache, linked into the checkout ----
phase_start cmake
link_cache_cmake "$cache"
plugin_bin=$(plugin_cmake)
if [[ -z "$plugin_bin" ]] || ! version_ge "$(cmake_version "$plugin_bin")" 3.30; then
  if command -v python3 >/dev/null 2>&1 && mkdir -p "$cache" 2>/dev/null &&
    { python3 -m venv "$cache/cmake-venv" >/dev/null 2>&1 || {
      apt_install python3-venv >/dev/null 2>&1 && python3 -m venv "$cache/cmake-venv" >/dev/null 2>&1
    }; } &&
    timeout 90 "$cache/cmake-venv/bin/python" -m pip install -q --timeout 20 --upgrade 'cmake>=3.30' >/dev/null 2>&1; then
    link_cache_cmake "$cache"
  else
    echo "  note: could not obtain CMake >= 3.30 (pip/pypi.org blocked?); the plugin build is unavailable, host tests are unaffected"
  fi
fi
phase_end

# --- submodule and references run concurrently (independent network fetches) ---------------------------------------
phase_start fetch
refs_log=$(mktemp) sdk_log=$(mktemp)
trap 'rm -f "$refs_log" "$sdk_log"' EXIT
mkdir -p "$cache" 2>/dev/null || true
refs_rc=0 sdk_rc=0
NEURO_FNV_MANAGED_DIR="$cache/managed-repos" python3 tools/references.py setup >"$refs_log" 2>&1 &
refs_pid=$!
ensure_submodule "$cache" 1 >"$sdk_log" 2>&1 &
sdk_pid=$!
wait "$sdk_pid" || sdk_rc=$?
wait "$refs_pid" || refs_rc=$?
sed 's/^/  /' "$sdk_log"
sed 's/^/  /' "$refs_log"
# The readiness report below turns SDK and reference gaps into the DEGRADED summary; here only the fallback runs.
if ((sdk_rc)); then
  if fetch_json_fallback 2>&1 | sed 's/^/  json.h fallback: /'; then :; fi
  json_available && echo "  note: fetched only vendor/json.h for the host tests"
fi
((refs_rc == 0 || refs_rc == 3)) || echo "  note: reference setup failed (exit $refs_rc)"
touch "$cache/.ready" 2>/dev/null || true
phase_end

# --- optional MSVC ------------------------------------------------------------------------------------------------
msvc_refused=0
if ((with_msvc)); then
  phase_start msvc
  if [[ "${NEURO_FNV_ACCEPT_MSVC_LICENSE:-}" != 1 ]]; then
    echo "MSVC is optional and was not provisioned: review Microsoft's license, then rerun with --with-msvc and NEURO_FNV_ACCEPT_MSVC_LICENSE=1." >&2
    msvc_refused=1
  elif ! apt_install wine wine64 python3 msitools libgcab-1.0-0 ca-certificates winbind; then
    degraded "optional MSVC prerequisites could not be installed (Wine packages); no compile was attempted"
  elif ! bash tools/setup-msvc.sh; then
    degraded "optional MSVC provisioning failed; no compile was attempted"
  fi
  phase_end
fi

total=$(elapsed "$total_t0")
budget="within the ~5 minute (300s) environment-cache budget"
((${total%.*} > 300)) && budget="OVER the ~5 minute (300s) environment-cache budget: the cache will not be built"
printf '[setup] total        %6ss (%s)\n\n' "$total" "$budget"
readiness_report
if ((msvc_refused)) && [[ "$soft" != 1 ]]; then exit 2; fi
finish
