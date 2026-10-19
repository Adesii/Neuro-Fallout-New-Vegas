#!/usr/bin/env bash
# PostToolUse hook: clang-format C/C++ files Claude just edited. Skips vendored/adapted trees
# (libs/, src/itr/, src/defs/) which the repository guide says must not be broadly reformatted.
file=$(jq -r '.tool_input.file_path // empty')
[[ -n "$file" && -f "$file" ]] || exit 0
root=${CLAUDE_PROJECT_DIR:-$PWD}
rel=${file#"$root"/}
case "$rel" in
src/itr/* | src/defs/* | libs/*) exit 0 ;;
src/*.c | src/*.cpp | src/*.h | src/*.hpp | src/*/*.c | src/*/*.cpp | src/*/*.h | src/*/*.hpp | src/*/*/*.c | src/*/*/*.cpp | src/*/*/*.h | src/*/*/*.hpp) ;;
*) exit 0 ;;
esac
command -v clang-format >/dev/null || exit 0
clang-format -i "$file"
exit 0
