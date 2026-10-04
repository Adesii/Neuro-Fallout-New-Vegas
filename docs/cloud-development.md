# Codex Cloud development environment

Neuro-FNV can be developed without a running copy of Fallout: New Vegas. The reusable environment supplies source tools, the pinned SDK submodule and a searchable reference corpus. Private menu XML and a real x86 MSVC toolchain are optional layers. This does not make FNV runnable in Cloud.

## Create and publish

Use the [current Codex Cloud environment guide](https://learn.chatgpt.com/docs/environments/cloud-environments): select this GitHub repository, start the environment setup conversation, review its report, then **Publish**. Publishing captures the prepared filesystem. Keep the environment private if it contains game extracts. Repository refresh does not rerun installation/startup commands; republish after changing infrastructure, reference pins or prepared private assets.

Give the setup agent this instruction:

> Prepare this repository using AGENTS.md and docs/cloud-development.md. Run `bash tools/cloud-setup.sh` from the repository root and record it as the Install script. Record `bash tools/cloud-setup.sh --check` as the startup readiness instruction; no service needs starting. Keep the public reference corpus and private extracts read-only and out of commits. Source/reference readiness is required; MSVC compilation is optional. Do not run Fallout, use launch_mo, deploy artifacts, invent engine details or claim in-game verification. Do not install Microsoft tools unless I explicitly authorize the license-acceptance/build opt-in described here. Report installed tools, reference revisions, optional XML status and actual compiler/linker results before publishing. Keep this environment private if menu extracts are present.

Base setup needs internet for package repositories, Python packages when CMake must be upgraded, GitHub source acquisition and the SDK submodule. Configure package-manager access plus source hosts used by `tools/reference-manifest.json`. Do not put credentials in scripts, tracked files or public URLs. Consult the environment UI's current secret/network controls if privately downloading prepared data; a local prepared archive requires no credentials.

For the [legacy environment UI](https://learn.chatgpt.com/docs/environments/cloud-environment), use `bash tools/cloud-setup.sh` as Setup script. Use `git submodule update --init --recursive && python3 tools/references.py setup && python3 tools/references.py check` as Maintenance script when setup network access is available. Both scripts are repeatable. Legacy shell exports do not persist into the agent phase; configure values in environment settings instead.

## Public references and local reuse

`tools/reference-manifest.json` locks upstream repositories to full commit IDs and describes their research purpose. These repositories are not linked into the plugin and their dependencies need not be built. Setup populates ignored `.reference-data/repos/` and writes ignored `.references`. Agents should read [reference-research.md](reference-research.md), not modify external repositories.

On a local machine, keep the existing valid two-line `.references`, or explicitly configure roots:

```bash
NEURO_FNV_REFERENCE_ROOT=/path/to/existing/reference-corpus \
NEURO_FNV_MENU_ROOT=/path/to/existing/extracts \
python3 tools/references.py setup
python3 tools/references.py check
```

The source root contains `FalloutNVAccess/`, `itr-nvse/`, `JIP-LN-NVSE/`, `JohnnyGuitarNVSE/`, `neuro-sdk/`, `NVSE/`, `sup/`, and `yUI-NVSE/`. The extracts root contains `menus/`. Local reuse creates canonical links rather than copying data, and reports actual local revisions rather than resetting them to pins. Set persistent overrides in your shell or Cloud environment settings, not in public configuration.

## Optional private menu data

Prepare the archive yourself from legally obtained local data. **Do not commit it or the extracted files.** The archive must contain only XML files underneath a top-level `menus/` directory, with directory structure preserved, for example:

```text
menus/inventory_menu.xml
menus/quantity_menu.xml
menus/chargen/love_tester_menu.xml
menus/prefabs/...xml
```

Include referenced XML templates where available. No extra wrapping directory, textures, sounds, executables or full game archives. Retain a private record of whether these are vanilla or modded UI assets and the relevant mod/version. Do not assume they match every user's installed UI.

Place the ZIP outside the Git checkout, for example `/tmp/neuro-fnv-private/menu-xml.zip`, during setup and set `NEURO_FNV_MENU_ARCHIVE` to that absolute path. Alternatively supply a prepared directory and set `NEURO_FNV_MENU_ROOT` to its parent of `menus/`. Provision the file through the environment setup conversation or your private storage workflow; this repository does not assume a particular Cloud file-upload API or fetch copyrighted assets. Upload availability depends on the Cloud UI; if it cannot attach files, have the setup agent download your private archive through your configured private access, without saving credentials in the checkout.

```bash
NEURO_FNV_MENU_ARCHIVE=/tmp/neuro-fnv-private/menu-xml.zip \
python3 tools/references.py setup
```

Setup validates archive paths/types and publishes data at ignored `.reference-data/extracts/menus/`. Missing optional data is explicitly reported and does not block public source setup. An explicitly configured missing or invalid archive is an error, not a silent fallback. Published environments contain prepared files: review access/sharing before publishing. A task may inspect XML, but must never stage it or paste substantial extracts into public artifacts.

Archive limits are 64 MiB total uncompressed data, 4 MiB per file and compression ratio 200. Identical repeated provisioning is allowed; differing already-prepared private data is preserved and reported as an error. To change it, deliberately replace the generated prepared-data layer in environment setup (or recreate the environment), then republish. Local source/extract overrides always remain untouched. `NEURO_FNV_MENU_ROOT` takes precedence if both private-data variables are set.

## Verification boundaries

Available without FNV:

- Offline corpus/submodule/tool readiness checks; source inspection and upstream research.
- `clang-format` with the repository `.clang-format`; limit formatting to changed C/C++ files, not vendored/reference trees.
- Real compiler/linker checks when the optional Microsoft toolchain is available, including compile-time layout assertions.
- Targeted deterministic checks for portable setup infrastructure. The plugin has no automated gameplay test suite or CTest targets.

Not available: native menu interaction, engine address correctness, input hooks, script-only calls compiled by xNVSE at runtime, frame timing, actual inventory mutation, UI-mod compatibility, Wine/game runtime stability or GECK ESM regeneration. Host GCC compilation and guessed mock engine layouts are not substitutes for the Windows x86 build. Clang-based editor diagnostics are advisory, especially with MSVC wrappers and missing Microsoft headers.

Feature handoffs must include actual evidence, checks run, unverified assumptions and a concrete manual in-game checklist; see [reference-research.md](reference-research.md). Keep manual verification notes in the task response and durable feature documentation where useful. Do not claim compile success proves native behavior.

## Optional Windows x86 compilation

The project requires CMake 3.30+, MSVC-compatible Windows x86 `cl`, linker/resource tools, Microsoft C/C++ runtime headers/libraries and Windows SDK, plus Wine on a Linux host. It uses MSVC inline assembly and fixed engine overlays; ordinary Linux GCC, MinGW presets and speculative compiler rewrites are not supported.

The portable build entry point is:

```bash
bash tools/build.sh Release
# Debug is also supported, but Wine/MSVC debug records may fail:
bash tools/build.sh Debug
# Or with an existing non-default installation:
NEURO_FNV_MSVC_ROOT=/path/to/msvc bash tools/build.sh Release
```

Build artifacts stay in ignored `build/cloud/` by default; `NEURO_FNV_BUILD_DIR` overrides the location. Compiler Wine state is isolated at `.cloud-tools/wine/`, not the game's prefix; override only deliberately with `NEURO_FNV_BUILD_WINEPREFIX`. Keep `FalloutNVNeuroPlugin` unset in Cloud: deployment is local-only and the ESM is never automatically rebuilt/deployed. `just debug` builds incrementally; `just rebuild` / `just rebuild-debug` clean target outputs first without deleting the build tree, and `just release` selects Release. Existing legacy `build/` output is left untouched.

Microsoft tools are **not redistributable**. The optional installer uses [mstorsjo/msvc-wine](https://github.com/mstorsjo/msvc-wine) to obtain licensed tools from Microsoft; review the [Microsoft license](https://go.microsoft.com/fwlink/?LinkId=2086102) and repository installer instructions before opting in. Downloads, Wine, host package installation, available Microsoft manifests and Cloud network policy can prevent provisioning. Source/reference setup must remain usable if optional provisioning fails. Do not publish toolchain binaries in this Git repository or export them as reusable public artifacts.

After personally reviewing and accepting the Microsoft terms, optional provisioning is:

```bash
NEURO_FNV_ACCEPT_MSVC_LICENSE=1 bash tools/cloud-setup.sh --with-msvc
bash tools/build.sh Release
```

The installer pins `msvc-wine` to commit `514f8ea34842cd6d831804d0e9658d3a32870ae1` and requests Visual Studio toolset version `18.0` plus Windows SDK `10.0.26100`, targeting x86 through x64-hosted compiler tools. Microsoft manifests/payload availability are external and mutable; this is not a fully hermetic compiler lock. Allow the package-manager/GitHub hosts plus `aka.ms`, `download.visualstudio.microsoft.com` and any Microsoft manifest redirect hosts actually observed during setup. The installer does not require Docker or a game installation. Package installation targets Ubuntu/Debian Cloud images, not arbitrary Linux distributions; local reuse only needs Python/Git and existing tools.

Debug builds prefer embedded `/Z7` compile information rather than separate compile PDBs; the linker can still emit a PDB. Compiler probes use Release and still compile/link real x86 code. Debug-record and Wine-prefix errors remain possible; report them rather than skipping compiler checks or claiming a DLL was produced. Start with Release for Cloud compile verification.


Run the build during environment preparation if you enable compilation; report the actual compiler/linker result. A successful build on a local MSVC-over-Wine installation does not establish that provisioning works on a fresh Cloud image. Republish after installing the compiler and verify a new task can still invoke it.

## Local setup verification

- Fresh acquisition of all eight pinned public repositories passed, as did repeated setup and offline checks with private menus absent.
- Existing local source and extracts roots were reused through links. Local unversioned SUP and pre-existing dirty SDK state were reported, not modified.
- Synthetic ZIP smoke checks passed for valid/idempotent intake, rejection of unsafe paths, symlinks, non-XML files, oversized/high-ratio payloads and duplicate members, and staging isolation from external extracts.
- Shell parsing, readiness checks from another working directory, CMake preset discovery and refusal to provision MSVC without explicit license acknowledgment passed.
- Full DLL compilation was **not established** on the local host: MSVC 19.51 over Wine failed Debug probes with `D8050`; a Release probe compiled but failed linking with `LNK1104` on a Wine temporary file. The isolated compiler-prefix Release attempt timed out. Fresh Ubuntu/Cloud package installation and Microsoft downloads were not exercised locally. Treat optional compilation as an environment capability to validate during setup, not a readiness requirement or a promised success.

