# Cloud development environments

Neuro-FNV can be developed without a running copy of Fallout: New Vegas. A cloud environment supplies source tools, the pinned SDK submodule, the host-side unit tests and a searchable reference corpus. Private menu XML and a real x86 MSVC toolchain are optional layers. This does not make FNV runnable in the cloud, and a compile or host test is not in-game verification.

This document covers **Claude Code cloud** and **Codex Cloud** with the same repository scripts. [CLAUDE.md](../CLAUDE.md) stays the single source of truth for project rules; [AGENTS.md](../AGENTS.md) only points to it.

## What the scripts do

| Script | Purpose |
| --- | --- |
| `bash tools/cloud-setup.sh` | Install only the missing base tools, initialize `libs/libneurosdk`, fetch the pinned references (shallow, in parallel), print a timing line per phase and a readiness report. Works as root, or as a user with passwordless `sudo`; without either it installs nothing and reports what is missing. Idempotent. Network steps are time-bounded (60 s per git command, `apt` 120 s): a proxy that hangs makes the setup finish `DEGRADED` after about two minutes instead of stalling the five-minute cache window. |
| `bash tools/cloud-setup.sh --check` | Offline, no installs, no fetches, about a second. Same readiness report. |
| `bash tools/ensure-env.sh` | Per-session readiness step. Does nothing unless `CLAUDE_CODE_REMOTE=true` or `--force`. Re-links references and the SDK from the cache without network, fetches only what is missing (time-bounded), installs the CMake >= 3.30 upgrade into the cache when it is absent (so a missing or stale setup script self-heals), prints a one-screen summary. Always exits 0. |
| `python3 tools/references.py hosts` | Prints the hosts the pinned references are fetched from (the allowlist input below). |

**Exit status of `cloud-setup.sh` and `--check`:** `0` ready, `3` degraded (something optional or network-dependent is unavailable; everything else stays usable), `1` a required step failed, `2` usage error. `--exit-zero` (or `NEURO_FNV_SETUP_SOFT=1`) prints the same report but always exits 0. Use it wherever a non-zero setup exit stops the session from starting (Claude Code cloud).

**Cache directory.** References and a mirror of the pinned SDK commit are stored outside the checkout so a re-cloned repository can be re-linked offline: `$NEURO_FNV_CACHE_DIR`, else `/opt/neuro-fnv-cache` when running as root, else `.cloud-tools/cache` inside the checkout. An existing directory containing the `.ready` marker always wins.

**What degraded means** (the report names each case):

- *libneurosdk unavailable*: the host tests need `libs/libneurosdk/vendor/json.h`. When the submodule cannot be cloned, the setup fetches only that file at the pinned commit from `raw.githubusercontent.com`, checks its SHA-256 and stores it in the ignored `.cloud-tools/sdk-fallback/`, so `bash tools/test.sh` still runs. If that fails as well, the host tests cannot be built. When a `libneurosdk` bump changes `vendor/json.h`, update `SDK_JSON_SHA256` in `tools/lib/env.sh`, otherwise the fallback fails its hash check. The plugin build always needs the real submodule.
- *a reference unavailable*: that reference is reported with its host and the last git error line (for example a proxy `403`); the others are still linked.
- *CMake older than 3.30*: only the plugin build is affected (the host tests need CMake 3.20). The setup tries `pip install cmake` into the cache.

## Claude Code cloud

Checked against the [environment](https://code.claude.com/docs/en/cloud-environments) and [cloud sessions](https://code.claude.com/docs/en/claude-code-on-the-web) docs. Every session gets a fresh Ubuntu 24.04 x86_64 VM (about 4 vCPU, 16 GB RAM, 30 GB disk) with a fresh clone of the repository: only committed files exist, so ignored files, `~/.claude` and local paths do not. The image already has git, gh, jq, ripgrep, Python 3, GCC/Clang, cmake and ninja. Wine is not preinstalled. The cmake version and whether `clang-format` is present are not documented; the scripts check and install what is missing.

### Environment settings

Create an environment at claude.ai/code and select this repository.

1. **Network access: Custom**, tick **Also include default list of common package managers**, and add under *Allowed domains*:

   ```text
   git.alex19ep.me
   ```

   The default (Trusted) list already contains everything else the base setup needs: `github.com` (SDK submodule and 8 of 9 references), `raw.githubusercontent.com` (json.h fallback), `pypi.org` and `files.pythonhosted.org` (CMake upgrade), `archive.ubuntu.com` and `security.ubuntu.com` (apt). Staying on plain **Trusted** works but leaves `FalloutNVAccess` (served by `git.alex19ep.me`) missing and the setup reports `DEGRADED`. **None** blocks every fetch. The docs say GitHub traffic goes through a proxy that serves the repositories attached to the session and refuses archive downloads of others, even public ones; they do not say whether `git clone`/`git fetch` of *other* GitHub repositories works. The scripts handle both outcomes (see *What degraded means*); the first real session tells you which one you got.
2. **Setup script** (runs as root before Claude starts, in a separate step from the session; environment variables are **not** set yet, so set anything the script needs inside it). It must exit 0, finish in roughly five minutes to be cached, and is skipped while the cache exists. Paste:

   ```bash
   #!/bin/bash
   repo="${CLAUDE_PROJECT_DIR:-$PWD}"
   if [ -f "$repo/tools/cloud-setup.sh" ]; then
     NEURO_FNV_SETUP_SOFT=1 bash "$repo/tools/cloud-setup.sh"
   else
     echo "neuro-FNV checkout not found at $repo; installing base tools only (the SessionStart hook finishes the setup)."
     apt-get update -qq && apt-get install -y -qq jq clang-format ripgrep ninja-build || true
   fi
   exit 0
   ```

   The docs do not say where the repository is checked out during the setup script, hence the fallback. The SessionStart hook is the authoritative step.
3. **Environment variables** (`.env` format; read when the session starts, so they reach the hook but not the setup script): none are required. Optional: `NEURO_FNV_REF_JOBS=9`, `NEURO_FNV_GIT_TIMEOUT=60`. Values are readable by anyone who can use the environment; never put credentials there.

### Per-session hook

`.claude/settings.json` registers a `SessionStart` hook (`startup|resume`, 300 s timeout) that runs `tools/ensure-env.sh` only when `CLAUDE_CODE_REMOTE=true`. It runs on every start, including when the environment cache skipped the setup script, and prints the readiness summary into the session. Locally it prints nothing. The repository relies on nothing in `~/.claude`: the format hook needs only `jq` and `clang-format`.

Limits from the docs: hooks from `.claude/settings.json` do not run in sessions with several repositories or in a project's cloud threads. There, run `bash tools/ensure-env.sh --force` yourself.

### Checks

```bash
bash tools/cloud-setup.sh --check   # offline readiness, exit 0/3/1
bash tools/test.sh                  # host unit tests (engine-independent logic only)
python3 tools/references.py check   # reference revisions, one block per reference
```

## Codex Cloud

Checked against the [Cloud environments](https://learn.chatgpt.com/docs/environments/cloud-environments) page, the [legacy environment](https://learn.chatgpt.com/docs/environments/cloud-environment) and [internet access](https://learn.chatgpt.com/docs/cloud/internet-access) pages and the [AGENTS.md](https://learn.chatgpt.com/docs/agent-configuration/agents-md) page. The current page describes a conversational flow (create environment, select the repository, let Codex test the setup, **Publish**); the explicit setup/maintenance script fields are documented on the legacy page. Which of the two applies to your workspace depends on the UI you see, so both are given. Things the docs do not state (script user, time limit, snapshot details, submodule handling, custom-domain syntax, whether `CLAUDE.md` is read) are **unverified**.

### Environment settings

1. **Repository:** this repository. Codex reads `AGENTS.md` (merged from the repository root down, 32 KiB combined by default); `AGENTS.md` points to `CLAUDE.md`.
2. **Setup script:** `bash tools/cloud-setup.sh`. Setup scripts run with internet access; the legacy page says the agent phase has none by default, so everything that needs the network happens here. Use the plain form (no `--exit-zero`) so a degraded exit code `3` stays visible; add `--exit-zero` if your environment treats a non-zero setup exit as a failure.
3. **Maintenance script** (runs when a cached container is resumed; the legacy page says the cache lasts up to 12 hours): `bash tools/ensure-env.sh --force`. It re-links from the cache and fetches only what is missing, so a stale container recovers. Setup scripts run in a separate shell: exports do not persist into the agent phase.
4. **Internet access:** keep the agent phase off or restricted. If you restrict to a domain allowlist, use the **Common dependencies** preset and add `git.alex19ep.me`; whether the setup phase honours the allowlist is not documented. If the agent phase restricts methods to GET, HEAD and OPTIONS, `git fetch` over HTTPS (which POSTs) fails there; the references prepared during setup avoid needing it.
5. **Environment variables / secrets:** none required. Secrets are only available to setup scripts (legacy page). Changing scripts, variables or secrets invalidates the cache; use *Reset cache* after changing reference pins.
6. If you use the setup conversation, give the setup agent this instruction:

   > Prepare this repository using AGENTS.md, CLAUDE.md and docs/cloud-development.md. Record `bash tools/cloud-setup.sh` as the setup script and `bash tools/ensure-env.sh --force` as the maintenance script; `bash tools/cloud-setup.sh --check` is the offline readiness check. Keep the public reference corpus and private extracts read-only and out of commits. Source and reference readiness matter; MSVC compilation is optional. Do not run Fallout, use launch_mo, deploy artifacts, invent engine details or claim in-game verification. Do not install Microsoft tools unless I explicitly authorize the license-acceptance opt-in described there. Report installed tools, reference revisions and the actual setup exit status before publishing. Keep this environment private if menu extracts are present.

   Publishing captures the prepared filesystem and makes it available to new tasks; republish after changing infrastructure, reference pins or prepared private assets. Keep the environment private if it contains game extracts.

### Checks

Same commands as in Claude Code cloud.

## Environment variables

| Variable | Effect |
| --- | --- |
| `NEURO_FNV_CACHE_DIR` | Cache location (references, SDK mirror, upgraded CMake). See above for the default. |
| `NEURO_FNV_SETUP_SOFT=1` | Same as `cloud-setup.sh --exit-zero`. |
| `NEURO_FNV_REFERENCE_ROOT`, `NEURO_FNV_MENU_ROOT` | Reuse existing local reference repositories / extracts (links, never copies). |
| `NEURO_FNV_MENU_ARCHIVE` | Absolute path of a private menu ZIP (see below). |
| `NEURO_FNV_MANAGED_DIR` | Where `references.py` clones missing references (set by the scripts to `<cache>/managed-repos`). |
| `NEURO_FNV_OFFLINE=1` | `references.py setup` never contacts a remote (`setup --offline` does the same). |
| `NEURO_FNV_REF_JOBS`, `NEURO_FNV_GIT_TIMEOUT` | Parallel reference fetches (default 9) and per-git-command timeout in seconds (default 60; the SessionStart hook uses 30). |
| `NEURO_FNV_MSVC_ROOT`, `NEURO_FNV_ACCEPT_MSVC_LICENSE` | Optional MSVC, see below. |
| `NEURO_FNV_BUILD_DIR`, `NEURO_FNV_BUILD_WINEPREFIX`, `NEURO_FNV_TEST_BUILD_DIR` | Build outputs and the isolated compiler Wine prefix. |

Keep `FalloutNVNeuroPlugin` unset in the cloud: deployment is local-only and the ESM is never rebuilt or deployed automatically.

## Public references and local reuse

`tools/reference-manifest.json` locks upstream repositories to full commit IDs and describes their research purpose. These repositories are not linked into the plugin and their dependencies need not be built. Setup populates the ignored `.reference-data/repos/` and writes the ignored `.references`. Agents should read [reference-research.md](reference-research.md), not modify external repositories.

Hosts the manifest needs (`python3 tools/references.py hosts`): `github.com` (eight references) and `git.alex19ep.me` (`FalloutNVAccess`). The SDK submodule (`.gitmodules`) is on `github.com` too. Each pinned commit is fetched with `git fetch --depth 1 <url> <full-sha>`, falling back to a blobless clone for servers that refuse fetch-by-SHA. Fetch-by-SHA was tested successfully against both real hosts from a development machine; the blobless fallback was only exercised against a simulated refusal.

On a local machine, keep the existing valid two-line `.references`, or explicitly configure roots:

```bash
NEURO_FNV_REFERENCE_ROOT=/path/to/existing/reference-corpus \
NEURO_FNV_MENU_ROOT=/path/to/existing/extracts \
python3 tools/references.py setup
python3 tools/references.py check
```

The source root contains `FalloutNVAccess/`, `itr-nvse/`, `JIP-LN-NVSE/`, `JohnnyGuitarNVSE/`, `neuro-sdk/`, `NVSE/`, `sup/`, and `yUI-NVSE/`; `Neuro-Skyrim-Integration/` is also in the manifest. The extracts root contains `menus/`. Local reuse creates canonical links rather than copying data, and reports actual local revisions rather than resetting them to pins.

## Optional private menu data

Prepare the archive yourself from legally obtained local data. **Do not commit it or the extracted files.** The archive must contain only XML files underneath a top-level `menus/` directory, with directory structure preserved, for example:

```text
menus/inventory_menu.xml
menus/quantity_menu.xml
menus/chargen/love_tester_menu.xml
menus/prefabs/...xml
```

Include referenced XML templates where available. No extra wrapping directory, textures, sounds, executables or full game archives. Retain a private record of whether these are vanilla or modded UI assets and the relevant mod/version. Do not assume they match every user's installed UI.

Place the ZIP outside the Git checkout and set `NEURO_FNV_MENU_ARCHIVE` to its absolute path, or supply a prepared directory and set `NEURO_FNV_MENU_ROOT` to its parent of `menus/`:

```bash
NEURO_FNV_MENU_ARCHIVE=/tmp/neuro-fnv-private/menu-xml.zip python3 tools/references.py setup
```

How the ZIP reaches a cloud VM is **not** covered by this repository and is unverified for both services: Claude Code cloud documents no file upload (environment variables are not visible to the setup script, but are visible to the SessionStart hook, whose offline `references.py setup` honours `NEURO_FNV_MENU_ARCHIVE`); in Codex use the setup conversation or your private storage workflow. Never put credentials in tracked files or public URLs. Setup validates archive paths and types and publishes data at ignored `.reference-data/extracts/menus/`. Missing optional data is reported and does not block public source setup; an explicitly configured missing or invalid archive is an error. Published or cached environments contain prepared files: review access and sharing first. A task may inspect XML, but must never stage it or paste substantial extracts into public artifacts.

Archive limits are 64 MiB total uncompressed data, 4 MiB per file and compression ratio 200. Identical repeated provisioning is allowed; differing already-prepared private data is preserved and reported as an error. To change it, recreate the environment (or its cache) and republish. `NEURO_FNV_MENU_ROOT` takes precedence if both variables are set.

## Verification boundaries

Available without FNV:

- Offline readiness checks, source inspection and upstream research.
- Host-side unit tests (`bash tools/test.sh`): engine-independent logic against a fake NeuroSDK transport. They need the SDK's `vendor/json.h` and prove nothing about engine, menu or in-game behaviour.
- `clang-format` with the repository `.clang-format`; limit formatting to changed C/C++ files, not vendored or reference trees.
- Real compiler/linker checks when the optional Microsoft toolchain is available, including compile-time layout assertions.

Not available: native menu interaction, engine address correctness, input hooks, script-only calls compiled by xNVSE at runtime, frame timing, actual inventory mutation, UI-mod compatibility, Wine/game runtime stability or GECK ESM regeneration. Host GCC compilation and guessed mock engine layouts are not substitutes for the Windows x86 build. Clang-based editor diagnostics are advisory, especially with MSVC wrappers and missing Microsoft headers.

Feature handoffs must include actual evidence, checks run, unverified assumptions and a concrete manual in-game checklist; see [reference-research.md](reference-research.md). Do not claim compile success proves native behavior.

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

Build artifacts stay in ignored `build/cloud/` by default; `NEURO_FNV_BUILD_DIR` overrides the location. Compiler Wine state is isolated at `.cloud-tools/wine/`, not the game's prefix. `just debug` builds incrementally; `just rebuild` / `just rebuild-debug` clean target outputs first without deleting the build tree, and `just release` selects Release. Existing legacy `build/` output is left untouched.

Microsoft tools are **not redistributable**. The optional installer uses [mstorsjo/msvc-wine](https://github.com/mstorsjo/msvc-wine) to obtain licensed tools from Microsoft; review the [Microsoft license](https://go.microsoft.com/fwlink/?LinkId=2086102) and the repository installer instructions before opting in. The scripts never accept the license for you. Do not publish toolchain binaries in this Git repository or export them as reusable public artifacts.

After personally reviewing and accepting the Microsoft terms, optional provisioning is:

```bash
NEURO_FNV_ACCEPT_MSVC_LICENSE=1 bash tools/cloud-setup.sh --with-msvc
bash tools/build.sh Release
```

The installer pins `msvc-wine` to commit `514f8ea34842cd6d831804d0e9658d3a32870ae1` and requests Visual Studio toolset version `18.0` plus Windows SDK `10.0.26100`, targeting x86 through x64-hosted compiler tools. Microsoft manifests and payloads are external and mutable; this is not a fully hermetic compiler lock. Package installation targets Ubuntu/Debian cloud images; local reuse only needs Python/Git and existing tools.

**Extra network hosts for MSVC** (in addition to the base hosts above):

- `github.com`: `tools/setup-msvc.sh` clones `mstorsjo/msvc-wine`, which is not the session's repository, so a proxy may refuse it.
- `archive.ubuntu.com`, `security.ubuntu.com`: Wine and its dependencies through apt (in the Claude Trusted defaults).
- `aka.ms`: read from the pinned `vsdownload.py`, which requests the Visual Studio channel manifest at `https://aka.ms/vs/<major>/release/channel`.
- `download.visualstudio.microsoft.com` and any host the manifest redirects to: the payload URLs come from Microsoft's manifest, not from this repository, so the exact list is **unverified**; add what you observe in a failed setup log. In Claude Code custom allowlists only a leading `*.` wildcard is documented, so `microsoft.com` does not cover this host.

The download is large and slow. Do not put `--with-msvc` into a setup script that must finish in about five minutes to be cached (Claude Code cloud); run it inside a session instead, or accept an uncached environment. Timing was not measured. Debug builds prefer embedded `/Z7` compile information; the linker can still emit a PDB. Compiler probes use Release and still compile and link real x86 code. Debug-record and Wine-prefix errors remain possible; report them rather than skipping compiler checks or claiming a DLL was produced. Start with Release for cloud compile verification. A successful build on a local MSVC-over-Wine installation does not establish that provisioning works on a fresh cloud image.

## Verification record

Earlier local work (before the agent-neutral rewrite): fresh acquisition of all eight then-pinned public repositories, repeated setup and offline checks; reuse of existing local roots through links; synthetic ZIP smoke checks for the menu archive rules; refusal to provision MSVC without license acknowledgment. Full DLL compilation was **not established** locally: MSVC 19.51 over Wine failed Debug probes with `D8050`; a Release probe compiled but failed linking with `LNK1104` on a Wine temporary file. Treat optional compilation as an environment capability to validate during setup, not a readiness requirement or a promised success.

The cloud setup scripts were exercised on a Linux development machine against throwaway clones, including simulated blocked hosts; the real Claude Code and Codex cloud proxies, the five-minute cache timing and MSVC provisioning have **not** been exercised.
