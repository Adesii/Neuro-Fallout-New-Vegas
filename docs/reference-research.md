# FNV reference research

Read this before implementing unknown engine or menu behavior. External sources are a **read-only research corpus**, not additional build dependencies or editable parts of Neuro-FNV.

## Finding the corpus

Run `python3 tools/references.py setup`, then `python3 tools/references.py check`. Setup writes an ignored `.references` containing resolved source/extract roots, one directory per line. Stable project-relative search paths are:

- `.reference-data/repos/<repository>/`: public plugin/SDK reference sources.
- `.reference-data/extracts/menus/`: optional privately supplied menu XML.
- `tools/reference-manifest.json`: upstream URLs, pinned revisions, purpose and license pointers.

Existing valid local `.references` roots are reusable without copying the corpus. Explicit `NEURO_FNV_REFERENCE_ROOT` and `NEURO_FNV_MENU_ROOT` overrides take precedence. Do not modify, reset, format, build, or initialize recursive submodules in external reference directories. Local checkouts may differ from the manifest; report the actual revision used. Managed checkouts are pinned; updating the manifest is a deliberate infrastructure change, not a side effect of feature work.

## What to search

| Repository | Useful evidence / starting points |
| --- | --- |
| FalloutNVAccess | Concrete menu observation, inventory/Pip-Boy reading, dialogue, character creation and accessible interaction. Start in `src/` and its menu handlers. Its README explicitly targets **Vanilla UI Plus**; do not assume those tile paths exist in vanilla or another UI overhaul. |
| NVSE | xNVSE interfaces, plugin lifecycle, engine type declarations and script command implementation. Compare with the vendored definitions first; upstream revisions may differ. |
| JohnnyGuitarNVSE | Expanded Bethesda/Gamebryo classes, inventory/UI helpers, plugin commands. Closely related to the JG definitions already compiled here. |
| JIP-LN-NVSE | Engine function usage, inventory/extra-data handling, UI/script commands and reverse-engineered call patterns. Inspect `nvse/`, `internal/`, `functions_jip/`, and `functions_ln/`. |
| itr-nvse | Engine hooks, input, navigation/gameplay utility patterns; provenance for this project's adapted `src/itr/`. Inspect implementation as well as `FEATURES.md` and `THIRD_PARTY.md`. |
| yUI-NVSE | Inventory sorting/icons, tile/UI integration and alternative engine definitions (`yUI/`, `CommonLib/`, `nvse/`). Watch for UI modifications and borrowed-source licensing. |
| sup | SUP UI/HUD bar and tile manipulation, utility/plugin commands; start in `nvse_plugin_example/`. Treat absent or unclear licensing as a prohibition on copying, not permission. |
| neuro-sdk | Official protocol, `API/BEST_PRACTICES.md`, action/schema/context examples. Not the project's C transport dependency: that is the pinned `libs/libneurosdk` submodule. |

The table is a search map, not a blanket compatibility or license guarantee. Use the manifest and the exact source file's notices at the revision you inspect.

## Research order

1. Establish the current behavior and ownership in `src/`, existing assertions, adapted-source comments and the actions skill. Reuse the project's definitions and verified patterns rather than introducing competing overlays.
2. Search included `libs/nvse/`, `libs/nvse/JG/`, and `libs/common/` for declarations and implementations. Distinguish FNV runtime from editor, Fallout 3, and other Bethesda game branches.
3. For UI controls, search private XML when present alongside existing `src/Menus/` and `src/Utils/` helpers. Find the actual named tile, ancestors, traits, includes/templates, visibility/enabled conditions and pane identity.
4. Search the most relevant reference repository, then cross-check another compatible implementation when a detail remains uncertain. Match **32-bit FNV 1.4.0.525**, calling convention, form type, object lifetime and ownership. A declaration alone does not prove runtime semantics.
5. If the corpus lacks evidence, research upstream public sources/documentation where network access permits. Record the URL, revision, exact file/symbol and any UI/mod/runtime assumptions. If a critical fact remains unavailable, explain the missing evidence; do not fabricate it to complete the task.

Never invent or estimate engine addresses, layout sizes, offsets, vtable slots, menu IDs, control names or undocumented side effects. Do not infer an open menu from a retained singleton. Preserve packing and layout assertions; a successful compile only checks assertions against the compiler's layout, not that the overlay matches the game.

## Menu XML is evidence, not executable verification

Private data should retain `menus/.../*.xml` paths, including included templates. XML can establish control names, hierarchy, traits and declared UI rules. It cannot prove native `HandleClick` behavior, singleton lifetime, inventory ownership, action completion or compatibility with the user's active UI mods. These files may use engine-specific syntax; generic XML parser rejection alone is not evidence the game data is invalid.

If XML is absent, the reference corpus and existing menu implementations remain searchable. Say which controls are corroborated by compatible source and which interactions still need manual verification. Do not download game extracts from public mirrors, put their contents in commits, or paste substantial extracted XML into public notes.

## Provenance and adaptation

Before copying/adapting an implementation, inspect the exact repository/file license and third-party notices. Public availability is not a license. No license or incompatible terms means no copying. Preserve required copyright/license text, file-level obligations, and attribution to the plugin, revision and exact source path. Record provenance near adapted engine definitions or in the required license file. Existing examples: `src/defs/FalloutNVAccess-license.md` and `src/itr/itr-license.md`.

Separate independently established engine facts from copied expression. Do not label code 'independent' merely because symbols were renamed. If evidence conflicts, record the disagreement and prefer this project's verified behavior until resolved.

## Handoff for changes Cloud cannot prove

In the task's final response, distinguish:

- **Evidence:** exact project/reference files, revisions, XML paths and relevant symbols supporting the implementation.
- **Checks actually run:** setup validation, formatting, compilation/static checks and their results. Name missing tools or checks that failed; never imply Cloud ran FNV.
- **Assumptions / unverified behavior:** native side effects, control activation, UI-mod compatibility, asynchronous completion and timing.
- **Manual in-game checklist:** concrete starting state, action, expected visible result/context, failure symptom and relevant logs. Include invalid/stale requests, higher-priority popups, menu close/reopen, disconnect/load reset and restoration of normal controls where affected.

For Pip-Boy inventory work, explicitly test registration only in the intended menu/tab, paging/filter/index identity, stack and extra-data identity, equip/unequip success, native confirmation dialogs, menu closure and Walker lockout/recovery. These are scenario prompts, not permission to extend feature scope. A compiler success does not close this checklist.
