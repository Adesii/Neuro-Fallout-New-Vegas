# AGENTS.md

[CLAUDE.md](CLAUDE.md) is the canonical project guide for every coding agent. Read it first; this file only repeats the
few rules a Codex session must not miss. If the two ever disagree, CLAUDE.md wins.

- Build, test and Cloud setup commands: see CLAUDE.md and [docs/cloud-development.md](docs/cloud-development.md).
  Set up with `bash tools/cloud-setup.sh`, re-check offline with `bash tools/cloud-setup.sh --check` (exit 0 ready,
  3 degraded, 1 failed) and run `bash tools/test.sh` for the host unit tests.
- The plugin is a 32-bit Windows DLL built with real MSVC over Wine. Never substitute MinGW or host GCC for it; the host
  tests in `tests/` are the only code built with the host compiler.
- Do not edit `libs/nvse`, `libs/common` or `libs/libneurosdk` (submodule), or the external reference repositories.
  Never commit `.references`, `.reference-data/`, `.cloud-tools/`, private menu extracts or credentials.
- Never invent engine addresses, layouts, offsets, menu IDs or control names. Compiling or passing host tests is not
  in-game verification: report the checks you ran, what stays unverified and a manual in-game checklist.
- Do not install Microsoft tools or accept their license on the user's behalf.
- Format changed C/C++ files with `clang-format -i` (repository `.clang-format`).
