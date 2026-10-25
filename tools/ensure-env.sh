#!/usr/bin/env bash
# Idempotent per-session readiness step for Claude Code cloud (SessionStart hook). Cheap when tools, SDK and
# references are already in place; does nothing locally. See docs/cloud-development.md.
#
# Usage: bash tools/ensure-env.sh [--force]
#   --force   run outside Claude Code cloud (for example in a session where hooks do not run, or to test this script)
#
# Always exits 0: a hook failure must never block a session. The summary on stdout is what Claude sees, so it stays short.
set -uo pipefail

[[ "${CLAUDE_CODE_REMOTE:-}" == true || "${1:-}" == --force ]] || exit 0

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="$ROOT_DIR/.cloud-tools/bin:$PATH"
cd "$ROOT_DIR" || exit 0
# shellcheck source=tools/lib/env.sh
source "$ROOT_DIR/tools/lib/env.sh"

t0=$(now_us)
cache=$(resolve_cache_dir)
echo "neuro-FNV environment (cache: $cache)"

# Short per-command timeouts keep the worst case well inside the hook's 300 s even with a proxy that accepts and hangs.
export NEURO_FNV_GIT_TIMEOUT=30 NEURO_FNV_HTTP_TIMEOUT=30 NEURO_FNV_APT_TIMEOUT=60

# Tools: install only what is missing (a cached environment normally has everything already).
mapfile -t missing < <(missing_tools "${REQUIRED_TOOLS[@]}" "${OPTIONAL_TOOLS[@]}")
if ((${#missing[@]})); then
  echo "Installing missing tools: ${missing[*]}"
  # shellcheck disable=SC2016  # the child shell expands $ROOT_DIR
  ROOT_DIR="$ROOT_DIR" timeout 130 bash -c 'source "$ROOT_DIR/tools/lib/env.sh"; install_missing_tools' >/dev/null 2>&1 || true
fi
link_cache_cmake "$cache"

export NEURO_FNV_MANAGED_DIR="$cache/managed-repos"
sdk_log=$(mktemp) refs_log=$(mktemp)
trap 'rm -f "$sdk_log" "$refs_log"' EXIT

# SDK submodule (from the cache mirror without network, else a bounded network attempt) and references (re-linked from
# the cache offline, else the missing ones fetched) run concurrently.
(
  if ! sdk_present; then
    if ! ensure_submodule "$cache" 0 >/dev/null 2>&1; then
      echo "libneurosdk not in cache; trying the network (bounded)."
      ensure_submodule "$cache" 1 2>&1
    fi
    sdk_present || fetch_json_fallback 2>&1
  fi
) >"$sdk_log" 2>&1 &
sdk_pid=$!
(
  if ! python3 tools/references.py setup --offline >/dev/null 2>&1; then
    echo "References not fully cached; fetching missing ones (bounded)."
    timeout 100 python3 tools/references.py setup 2>&1
  fi
) >"$refs_log" 2>&1 &
refs_pid=$!
wait "$sdk_pid" "$refs_pid"
cut -c1-170 "$sdk_log" | sed 's/^/  /' | head -n 4
cut -c1-170 "$refs_log" | sed 's/^/  /' | head -n 6

echo
readiness_report
if ((${#DEGRADED[@]} + ${#FAILED[@]} > 0)); then
  # Surface what the degradation costs, once, in plain words.
  json_available || echo "Host tests need libs/libneurosdk/vendor/json.h: 'bash tools/test.sh' will fail."
fi
printf '\nready check took %ss. Run: bash tools/test.sh (host tests); bash tools/cloud-setup.sh --check (offline status).\n' \
  "$(elapsed "$t0")"
summary_status || true
exit 0
