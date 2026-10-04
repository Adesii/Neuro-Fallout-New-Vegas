# Repository Guide

## Build and verification

- Initialize the SDK submodule with `git submodule update --init --recursive`.
- This is a 32-bit Windows DLL built on Linux with the MSVC-compatible wrapper and `/opt/msvc/bin/x86/cl`; ordinary host CMake configurations are rejected.
- Use `just rebuild` for a clean Debug configure/build. It deletes `build/`. Use `just debug` for an incremental build of an existing tree.
- Do not use `CMakePresets.json`: its presets reference the absent `cmake/toolchains/windows-i686-mingw.cmake` and do not match the MSVC-only flags in the root CMake file.
- Set `FalloutNVNeuroPlugin` before configuring to deploy `neuro-fnv.dll` and its PDB after each build to `$FalloutNVNeuroPlugin/nvse/plugins`. The ESM is not deployed by CMake.
- There are no automated tests, CTest targets, CI checks, or lint targets. Format changed C/C++ files with `clang-format -i <files>` (LLVM style, 120 columns), then build.

## Runtime architecture

- `src/plugin.cpp` owns `NVSEPlugin_Query`/`NVSEPlugin_Load`, interface acquisition, command registration, and the DirectInput hook. xNVSE's `kMessage_MainGameLoop` drives `NeuroSDK::MainLoop()`.
- `src/NeuroSDK.cpp` owns all libneurosdk polling/sending/message destruction. Each frame it copies incoming actions, runs `MenuHandler::Process()`, dispatches owned requests through `ActionRegistry`, then stops or processes Walker. `MenuHandler` handles UI/subtitle context and action execution lockout; `WalkerHandler` owns navigation, movement, and activation; `src/hooks/` supplies controlled DirectInput state.
- `src/CachedScripts.hpp` compiles and caches script-only xNVSE calls at runtime. Define wrappers with `CREATE_PLUGINSCRIPT`; plugin commands use `DEFINE_NEURO_COMMAND_PLUGIN`/`REG_CMD` and receive an `N` prefix (`SendContext` becomes `NSendContext`).

## Neuro actions and menu automation

- Before adding or changing NeuroSDK actions, action windows, registration/forcing/results, JSON schemas, or menu-backed visual execution, load the `neuro-fnv-actions` skill at `.agents/skills/neuro-fnv-actions/SKILL.md`.
- Keep one concrete action module under `src/Actions/<scope>/`; keep menu observation/window ownership/frame-driven execution under `src/Menus/`. Do not embed raw JSON schemas: use `Actions::Json::JsonSchema` and validate incoming `ActionData` locally.
- Required successful window-action order is validate, revalidate, locally close/unregister the window, send `action/result`, then execute. Invalid forced actions send `success: false` and remain available for Neuro's retry.
- Action windows are client-only and visual menu execution must lock out window regeneration and Walker until actual completion. Do not infer menu-open state from retained engine singleton pointers; use top/active menu state.
- Neuro supports only one active action force. The current implementation has no global force coordinator, so add coordination before introducing independently forceable systems.

### Container inventory scope

- `src/Menus/ContainerHandler.*` owns the five actions in `src/Actions/Container/`: `loot_item`, `query_items`, `loot_all`, `query_own_items`, and `stow_away_item`. They are registered only while ContainerMenu is the top menu, not in the Pip-Boy or barter menu. No container action force is issued.
- Opening a container publishes at most 15 stacks, or an explicit empty message. Query `index` is a zero-based page; item-transfer `index` is one-based (page 0 lists 1–15, page 1 lists 16–30). Optional `type` defaults locally to `All`; the shared `Inventory::ItemQueryType` values are `All`, `Weapons`, `Equipment`, `Consumerable`, `WeaponMods`, `Keys`, and `Misc`.
- Categories use engine form types, not item names: `Weapons` includes weapons and ammunition; `Equipment` includes armor and clothing; `Consumerable` includes aid items (food, drinks, chems) and ingredients; `WeaponMods` includes weapon attachments; `Keys` includes keys. Books, notes, bottles, junk, miscellaneous crafting components, currency and other remaining inventory forms fall under `Misc`. Native form classification lives in `src/Inventory/ItemClassification.cpp`; shared query parsing/schema construction lives in `src/Inventory/ItemQuery.cpp`.
- Container and own-inventory listings are independent, last-published-page snapshots. Transfers tombstone accepted indexes rather than compacting them; only a new query for that source replaces its bindings. Stowing requires `query_own_items` first. Opening supplies the initial loot bindings; indexes outside that listing require `query_items`.
- Transfers capture source pane, form ID, extra-data identity and stack count, then resolve against live menu rows before visual selection and activation. Ambiguous, removed, or changed stacks fail rather than substituting another item. Accepted different-index transfers run FIFO, using native quantity controls for whole stacks; queries and Take All wait until queued transfers finish.
- Take All uses the native `CM_TakeAllButton`, not direct inventory mutation. Popup handling remains native; the executor only confirms a quantity prompt after its own item activation. Menu closure, container replacement, disconnect and load reset both snapshots and queued work. Engine UI behavior still requires manual in-game verification.

## External plugin references

- Read `.references` for local directories containing other Fallout: New Vegas plugin sources. These directories are reference material, not part of this repository.
- Search the current repository and included NVSE/JG definitions first. If an engine definition, function, or behavior is missing or incomplete, search the referenced plugins for compatible declarations and proven implementation patterns.
- Before copying or adapting code, inspect the source plugin's license and provenance. Follow its license terms, preserve required notices, credit the plugin and exact source file near the adapted code or in the required license file, and place copied license text where that license requires it. Do not copy code whose license is absent or incompatible.

## Hard constraints and boundaries

- The plugin is tied to 32-bit Fallout: New Vegas `1.4.0.525` (`RUNTIME_VERSION=0x040020D0`) and uses absolute engine addresses plus memory-layout overlays. Do not change architecture, packing, structure layouts, or runtime version without auditing hooks, `src/defs/`, and `src/itr/` together.
- `libs/libneurosdk/` is a Git submodule; its `vendor/` is third-party code. `libs/nvse/` and `libs/common/` are vendored engine/plugin sources compiled selectively by the root CMake file. Avoid broad formatting or incidental edits in these trees.
- `src/itr/` is adapted from `itr-nvse`; preserve `src/itr/itr-license.md` and its fixed-address assumptions when changing pathfinding code.
- `esms/Neuro_FNV.esm` is binary data. Editable GECK scripts are under `scripts/Neuro_FNV.esm/`, but no checked-in command regenerates the ESM from them; do not assume script edits update the binary.
- `launch_mo` contains developer-specific Wine and Mod Organizer paths and is not a portable run command.
