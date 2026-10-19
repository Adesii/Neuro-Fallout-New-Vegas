# Repository Guide

## Build and verification

- Initialize the SDK submodule with `git submodule update --init --recursive`.
- This is a 32-bit Windows DLL built on Linux with real MSVC over Wine; ordinary host CMake configurations are rejected. Portable entry point: `bash tools/build.sh Debug` (CMake 3.30+, x86 toolchain at `NEURO_FNV_MSVC_ROOT` or `/opt/msvc`).
- Use `just debug` for incremental Debug compilation and `just rebuild` for a clean-first Debug build. Both use `build/cloud/` without deleting the existing local build tree; override with `NEURO_FNV_BUILD_DIR`. `just release` builds Release.
- Supported CMake presets use the repository's Windows x86 MSVC toolchain. Do not substitute MinGW or host GCC for compiler verification.
- Set `FalloutNVNeuroPlugin` before configuring to deploy `neuro-fnv.dll` and its PDB after each build to `$FalloutNVNeuroPlugin/nvse/plugins`. The ESM is not deployed by CMake.
- `bash tools/test.sh [name-filter]` (or `just test`) builds and runs the host-side unit tests in `tests/` with the host compiler. They cover engine-independent logic only (action registry/window/set state machines, `ActionData`, `JsonSchema`, item query parsing, `ItemListing`, `ScopedState`) against a fake NeuroSDK transport in `tests/stubs/` and `tests/FakeNeuro.*`; they prove nothing about engine, menu or in-game behaviour. Add a test beside any change to that logic; new engine-free sources must be added to `tests/CMakeLists.txt`.
- There are no CI checks or lint targets. Format changed C/C++ files with `clang-format -i <files>` (LLVM style, 120 columns), then build.
- For Cloud setup, private menu data, optional compiler provisioning and environment publication, read `docs/cloud-development.md`. Run `bash tools/cloud-setup.sh`; offline readiness: `bash tools/cloud-setup.sh --check`.
- Cloud cannot run FNV or prove engine/menu behavior. After relevant changes, report evidence and checks actually run, unresolved assumptions, and a concrete manual in-game checklist. Compilation is not runtime verification; no mock engine or fabricated addresses to claim completion.

## Runtime architecture

- `src/plugin.cpp` owns `NVSEPlugin_Query`/`NVSEPlugin_Load`, interface acquisition, command registration, and the DirectInput hook. xNVSE's `kMessage_MainGameLoop` drives `NeuroSDK::MainLoop()`.
- `src/NeuroSDK.cpp` owns all libneurosdk polling/sending/message destruction. Each frame it copies incoming actions, runs `MenuHandler::Process()`, dispatches owned requests through `ActionRegistry`, then stops or processes Walker. `MenuHandler` handles UI/subtitle context and action execution lockout; `WalkerHandler` owns navigation, movement, and activation; `src/hooks/` supplies controlled DirectInput state.
- `src/CachedScripts.hpp` compiles and caches script-only xNVSE calls at runtime. Define wrappers with `CREATE_PLUGINSCRIPT`; plugin commands use `DEFINE_NEURO_COMMAND_PLUGIN`/`REG_CMD` and receive an `N` prefix (`SendContext` becomes `NSendContext`).

## Neuro actions and menu automation

- Before adding or changing NeuroSDK actions, action windows, registration/forcing/results, JSON schemas, or menu-backed visual execution, invoke the `neuro-fnv-actions` skill (`.claude/skills/neuro-fnv-actions/SKILL.md`).
- Keep one concrete action module under `src/Actions/<scope>/`; keep menu observation/window ownership/frame-driven execution under `src/Menus/`. Do not embed raw JSON schemas: use `Actions::Json::JsonSchema` and validate incoming `ActionData` locally.
- Required successful window-action order is validate, revalidate, locally close/unregister the window, send `action/result`, then execute. Invalid forced actions send `success: false` and remain available for Neuro's retry.
- Action windows are client-only and visual menu execution must lock out window regeneration and Walker until actual completion. Do not infer menu-open state from retained engine singleton pointers; use top/active menu state.
- Neuro supports only one active action force. Preserve the process-wide force ownership guard in `src/Actions/ActionWindow.cpp` when adding independently forceable systems.

### Menu inventory scopes

- Container looting/stowing (`ContainerHandler`, `src/Actions/Container/`) and Pip-Boy inventory (`PipBoyHandler`, `InventoryHandler`) have detailed rules in `.claude/rules/container-inventory.md` and `.claude/rules/pipboy-inventory.md`. They load automatically when you read files under the matching `src/` paths; read them explicitly before designing changes elsewhere.
- Shared item query/classification code lives in `src/Inventory/`; both rules apply there.

## External plugin references

- Read `docs/reference-research.md` before researching unknown engine/menu behavior. Search the project and included NVSE/JG definitions first, then compatible reference implementations and private menu XML; prefer verified behavior over inference.
- `python3 tools/references.py setup` populates pinned public sources or reuses existing local roots. Read ignored `.references` for resolved roots; stable paths are `.reference-data/repos/` and optional `.reference-data/extracts/menus/`. Purpose/upstream/revision metadata lives in `tools/reference-manifest.json`.
- External repositories and private extracts are read-only research material, not editable project dependencies. Never reset/format them, commit copies, or commit copyrighted extracts, credentials, local configuration or binaries.
- Never invent engine addresses, layouts, offsets, menu IDs, control names or undocumented behavior. Match FNV 1.4.0.525 and distinguish source/XML-backed facts from assumptions needing runtime testing.
- Before copying or adapting code, inspect the exact source license/provenance; preserve required notices and credit the plugin, revision and source file. Public availability is not permission; no license or incompatible terms means no copying.

## Hard constraints and boundaries

- The plugin is tied to 32-bit Fallout: New Vegas `1.4.0.525` (`RUNTIME_VERSION=0x040020D0`) and uses absolute engine addresses plus memory-layout overlays. Do not change architecture, packing, structure layouts, or runtime version without auditing hooks, `src/defs/`, and `src/itr/` together.
- `libs/libneurosdk/` is a Git submodule; its `vendor/` is third-party code. `libs/nvse/` and `libs/common/` are vendored engine/plugin sources compiled selectively by the root CMake file. Avoid broad formatting or incidental edits in these trees.
- `src/itr/` is adapted from `itr-nvse`; preserve `src/itr/itr-license.md` and its fixed-address assumptions when changing pathfinding code.
- `esms/Neuro_FNV.esm` is binary data. Editable GECK scripts are under `scripts/Neuro_FNV.esm/`, but no checked-in command regenerates the ESM from them; do not assume script edits update the binary.
- `launch_mo` contains developer-specific Wine and Mod Organizer paths and is not a portable run command.
