# shellcheck shell=bash
# Shared helpers for tools/cloud-setup.sh and tools/ensure-env.sh. Source this file; do not execute it.
# Callers set ROOT_DIR before sourcing.

SDK_PATH=libs/libneurosdk
# sha256 of libs/libneurosdk/vendor/json.h at the pinned submodule commit. The host tests need only this header, so it
# is the one file fetched when the SDK repository itself cannot be cloned (see fetch_json_fallback).
SDK_JSON_SHA256=c89bc4b84e8b68450bc30f365070aab0739c86212564e8fd7517e17af5d89a16
JSON_FALLBACK_DIR="$ROOT_DIR/.cloud-tools/sdk-fallback/vendor"

REQUIRED_TOOLS=(git python3 cmake ninja c++)
OPTIONAL_TOOLS=(jq clang-format rg)
declare -A TOOL_PACKAGE=([git]=git [python3]=python3 [cmake]=cmake [ninja]=ninja-build [c++]=g++
  [jq]=jq [clang-format]=clang-format [rg]=ripgrep)

DEGRADED=()
FAILED=()
# Notes print immediately while a phase runs; readiness_report switches this off because the summary repeats them.
degraded() { DEGRADED+=("$1"); [[ "${NEURO_QUIET_NOTES:-0}" == 1 ]] || printf '[degraded] %s\n' "$1" >&2; }
failed() { FAILED+=("$1"); [[ "${NEURO_QUIET_NOTES:-0}" == 1 ]] || printf '[FAILED] %s\n' "$1" >&2; }

# EPOCHREALTIME's decimal separator follows the locale, so strip either one.
now_us() { local t=${EPOCHREALTIME/[.,]/}; printf '%s' "$t"; }
elapsed() { # elapsed <start_us> -> seconds with one decimal
  local d=$(($(now_us) - $1))
  printf '%d.%d' $((d / 1000000)) $((d % 1000000 / 100000))
}
phase_start() { PHASE_NAME=$1; PHASE_T0=$(now_us); }
phase_end() { printf '[setup] %-12s %6ss\n' "$PHASE_NAME" "$(elapsed "$PHASE_T0")"; }

# Run a command as root: directly, through passwordless sudo, or not at all (returns 1).
as_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  elif command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
    sudo -n "$@"
  else
    return 1
  fi
}
can_root() { [[ "$(id -u)" -eq 0 ]] || { command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; }; }

missing_tools() { # missing_tools <names...>
  local tool
  for tool in "$@"; do command -v "$tool" >/dev/null 2>&1 || printf '%s\n' "$tool"; done
}

# apt-get install with a package-list refresh once per process (a fresh VM image may ship empty lists).
APT_UPDATED=0
apt_install() {
  export DEBIAN_FRONTEND=noninteractive
  if ((!APT_UPDATED)); then
    as_root timeout "${NEURO_FNV_APT_TIMEOUT:-120}" apt-get update -qq >/dev/null || return 1
    APT_UPDATED=1
  fi
  as_root timeout "${NEURO_FNV_APT_TIMEOUT:-120}" apt-get install -y -qq "$@" >/dev/null
}

# Install only the base packages that are not already present. Sets nothing; callers re-check afterwards.
install_missing_tools() {
  local -a missing packages=()
  local tool
  mapfile -t missing < <(missing_tools "${REQUIRED_TOOLS[@]}" "${OPTIONAL_TOOLS[@]}")
  ((${#missing[@]})) || return 0
  for tool in "${missing[@]}"; do packages+=("${TOOL_PACKAGE[$tool]}"); done
  if ! can_root; then
    echo "Missing tools (${missing[*]}) and no root/sudo to install them." >&2
    return 1
  fi
  apt_install "${packages[@]}"
}

# Directory that survives a re-clone of the repository, so the setup script (cached by the Cloud environment) and the
# per-session ensure script share it. An existing marked directory always wins.
resolve_cache_dir() {
  local candidate
  for candidate in "${NEURO_FNV_CACHE_DIR:-}" /opt/neuro-fnv-cache "$ROOT_DIR/.cloud-tools/cache"; do
    [[ -n "$candidate" && -f "$candidate/.ready" ]] && { printf '%s' "$candidate"; return; }
  done
  if [[ -n "${NEURO_FNV_CACHE_DIR:-}" ]]; then printf '%s' "$NEURO_FNV_CACHE_DIR"
  elif [[ "$(id -u)" -eq 0 ]]; then printf '%s' /opt/neuro-fnv-cache
  else printf '%s' "$ROOT_DIR/.cloud-tools/cache"; fi
}

# "major.minor" of a cmake binary, empty if it does not run.
cmake_version() { "$1" --version 2>/dev/null | sed -n '1s/^cmake version \([0-9]*\.[0-9]*\).*/\1/p'; }
version_ge() { [[ -n "$1" && "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n1)" == "$2" ]]; }
# The cmake build.sh would use: the repo-local upgrade first, then PATH.
plugin_cmake() {
  if [[ -x "$ROOT_DIR/.cloud-tools/bin/cmake" ]]; then printf '%s' "$ROOT_DIR/.cloud-tools/bin/cmake"
  else command -v cmake || true; fi
}

sdk_expected_commit() { git -C "$ROOT_DIR" ls-tree HEAD "$SDK_PATH" | awk '{print $3}'; }
sdk_url() { git -C "$ROOT_DIR" config -f .gitmodules "submodule.$SDK_PATH.url"; }
sdk_present() { [[ -f "$ROOT_DIR/$SDK_PATH/CMakeLists.txt" ]]; }
json_available() { [[ -f "$ROOT_DIR/$SDK_PATH/vendor/json.h" || -f "$JSON_FALLBACK_DIR/json.h" ]]; }

# Bare, shallow mirror of the pinned SDK commit in the cache; later submodule init needs no network.
build_sdk_mirror() { # build_sdk_mirror <cache_dir>
  local mirror="$1/libneurosdk.git" sha url
  sha=$(sdk_expected_commit) url=$(sdk_url)
  [[ -d "$mirror" ]] && git --git-dir="$mirror" cat-file -e "$sha^{commit}" 2>/dev/null && return 0
  rm -rf "$mirror"
  mkdir -p "$1"
  git init -q --bare "$mirror" || return 1
  local rc=0
  GIT_TERMINAL_PROMPT=0 timeout "${NEURO_FNV_GIT_TIMEOUT:-60}" git --git-dir="$mirror" fetch -q --depth 1 "$url" "$sha:refs/heads/pinned" || rc=$?
  if ((rc)); then
    rm -rf "$mirror" # never leave an empty mirror behind: later runs would trust it
    ((rc == 124)) && echo "git fetch timed out after ${NEURO_FNV_GIT_TIMEOUT:-60}s"
    return 1
  fi
}

# Initialize libs/libneurosdk. Order: cache mirror (offline) -> mirror built from the network -> direct submodule fetch.
# Prints the reason and returns 1 when the SDK cannot be obtained.
ensure_submodule() { # ensure_submodule <cache_dir> <allow_network 0|1>
  local cache="$1" network="$2" mirror url
  sdk_present && return 0
  mirror="$cache/libneurosdk.git"
  url=$(sdk_url)
  local mirror_err=""
  if [[ "$network" == 1 ]]; then
    mirror_err=$(build_sdk_mirror "$cache" 2>&1)
    [[ -z "$mirror_err" ]] || printf '%s\n' "$mirror_err" | sed 's/^/  sdk mirror: /' >&2
  fi
  if [[ -d "$mirror" ]] && git --git-dir="$mirror" cat-file -e "$(sdk_expected_commit)^{commit}" 2>/dev/null; then
    git -C "$ROOT_DIR" submodule init -- "$SDK_PATH" >/dev/null 2>&1
    git -C "$ROOT_DIR" config "submodule.$SDK_PATH.url" "$mirror"
    if git -C "$ROOT_DIR" -c protocol.file.allow=always submodule update -q -- "$SDK_PATH"; then
      git -C "$ROOT_DIR" config "submodule.$SDK_PATH.url" "$url"
      git -C "$ROOT_DIR/$SDK_PATH" remote set-url origin "$url"
      return 0
    fi
    git -C "$ROOT_DIR" config "submodule.$SDK_PATH.url" "$url"
  fi
  # The direct clone is only worth trying when the server refused fetch-by-SHA; a network error or timeout would repeat.
  if [[ "$network" == 1 && "$mirror_err" =~ (not\ our\ ref|unadvertised|does\ not\ allow\ request|find\ remote\ ref) ]]; then
    GIT_TERMINAL_PROMPT=0 timeout "${NEURO_FNV_GIT_TIMEOUT:-60}" \
      git -C "$ROOT_DIR" submodule update --init --recursive -- "$SDK_PATH" 2>&1 | tail -n 2 | sed 's/^/  submodule: /' >&2
    sdk_present && return 0
  fi
  echo "libneurosdk unavailable from ${url#*//} (not in cache$([[ "$network" == 1 ]] && echo ', clone refused or failed'))." >&2
  return 1
}

# Last resort for the host tests: fetch only vendor/json.h at the pinned commit through raw.githubusercontent.com
# (a different path through the GitHub proxy than git) and verify it against the pinned hash.
fetch_json_fallback() {
  json_available && return 0
  local url sha slug
  url=$(sdk_url) sha=$(sdk_expected_commit)
  slug=${url#*github.com/}
  slug=${slug%.git}
  mkdir -p "$JSON_FALLBACK_DIR"
  python3 -I - "https://raw.githubusercontent.com/$slug/$sha/vendor/json.h" "$JSON_FALLBACK_DIR/json.h" \
    "$SDK_JSON_SHA256" "${NEURO_FNV_HTTP_TIMEOUT:-60}" <<'EOF' || { rm -f "$JSON_FALLBACK_DIR/json.h"; return 1; }
import hashlib, sys, urllib.request
url, dest, want, timeout = sys.argv[1:]
data = urllib.request.urlopen(url, timeout=int(timeout)).read()
if hashlib.sha256(data).hexdigest() != want:
    sys.exit(f"json.h from {url} does not match the pinned hash")
open(dest, "wb").write(data)
EOF
}

# Link .cloud-tools/bin/cmake to an upgraded cmake kept in the cache (survives a re-clone).
link_cache_cmake() { # link_cache_cmake <cache_dir>
  local cmake="$1/cmake-venv/bin/cmake"
  [[ -x "$cmake" ]] || return 0
  mkdir -p "$ROOT_DIR/.cloud-tools/bin"
  ln -sfn "$cmake" "$ROOT_DIR/.cloud-tools/bin/cmake"
}

# One-screen readiness report. Sets READY_* and appends to DEGRADED/FAILED. Offline and fast.
readiness_report() {
  local tool path v expected actual dirty plugin_bin refs_out
  NEURO_QUIET_NOTES=1
  for tool in "${REQUIRED_TOOLS[@]}" "${OPTIONAL_TOOLS[@]}"; do
    if path=$(command -v "$tool"); then
      case "$tool" in
        cmake) v=$(cmake_version "$path") ;;
        *) v=$("$path" --version 2>&1 | head -n1) ;;
      esac
      printf 'Tool %-13s %s\n' "$tool" "${v:-found}"
    else
      printf 'Tool %-13s MISSING\n' "$tool"
      if [[ " ${REQUIRED_TOOLS[*]} " == *" $tool "* ]]; then failed "required tool missing: $tool"
      else degraded "optional tool missing: $tool"; fi
    fi
  done
  v=$(cmake_version "$(command -v cmake || echo /nonexistent)")
  version_ge "$v" 3.20 || { [[ -z "$v" ]] || failed "cmake $v is too old for the host tests (needs 3.20)"; }
  plugin_bin=$(plugin_cmake)
  if [[ -z "$plugin_bin" ]] || ! version_ge "$(cmake_version "$plugin_bin")" 3.30; then
    degraded "plugin build unavailable: CMake >= 3.30 not found (host tests are unaffected)"
  fi
  expected=$(sdk_expected_commit)
  if sdk_present; then
    actual=$(git -C "$ROOT_DIR/$SDK_PATH" rev-parse HEAD)
    dirty=no
    [[ -z "$(git -C "$ROOT_DIR/$SDK_PATH" status --porcelain)" ]] || dirty=yes
    printf 'SDK libneurosdk %s (%s; dirty=%s)\n' "${actual:0:12}" "$([[ $actual == "$expected" ]] && echo pinned || echo "UNPINNED, expected ${expected:0:12}")" "$dirty"
    [[ "$actual" == "$expected" ]] || degraded "libneurosdk is not at the pinned commit"
  elif json_available; then
    printf 'SDK libneurosdk MISSING; json.h fallback present (host tests only)\n'
    degraded "libneurosdk submodule missing; host tests use the json.h fallback, the plugin build is unavailable"
  else
    printf 'SDK libneurosdk MISSING and no json.h fallback\n'
    degraded "libneurosdk unavailable: host tests (tests/ need vendor/json.h) and the plugin build cannot run"
  fi
  if refs_out=$(python3 "$ROOT_DIR/tools/references.py" check --brief 2>/dev/null); then
    printf '%s\n' "$refs_out"
  else
    printf '%s\n' "$refs_out"
    degraded "references missing or unpinned (source and tests still usable); fetch errors are printed by 'python3 tools/references.py setup'"
  fi
  local msvc_root="${NEURO_FNV_MSVC_ROOT:-/opt/msvc}"
  if [[ -x "$msvc_root/bin/x86/cl" && -x "$msvc_root/bin/x86/rc" && -x "$msvc_root/bin/x86/link" &&
    -d "$msvc_root/cmake/find_root/x86" ]]; then
    printf 'MSVC x86        installed (%s)\n' "$msvc_root"
  else
    printf 'MSVC x86        not installed (optional; %s)\n' "$msvc_root"
  fi
}

# Print DEGRADED/FAILED and return 0 ready, 3 degraded, 1 failed.
summary_status() {
  local item
  if ((${#FAILED[@]})); then
    echo "STATUS: FAILED"
    for item in "${FAILED[@]}"; do echo "  - $item"; done
  fi
  if ((${#DEGRADED[@]})); then
    echo "STATUS: DEGRADED"
    for item in "${DEGRADED[@]}"; do echo "  - $item"; done
  fi
  if ((${#FAILED[@]})); then return 1; fi
  if ((${#DEGRADED[@]})); then return 3; fi
  echo "STATUS: READY"
}
