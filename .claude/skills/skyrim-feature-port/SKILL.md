---
name: skyrim-feature-port
description: Use before implementing a Neuro-facing feature that exists in the Skyrim integration (e.g. poking/hints, context updates, suppression rules). Researches the Skyrim behavior, writes a design note, then adapts it to this codebase.
---

# Port a Skyrim feature idea to neuro-fnv

Input: a feature name and optionally a `todo/` file describing it.

## 1. Study the Skyrim side (read-only)
Find the feature in the Skyrim integration (resolve its root via `.references`). Do not copy code. Extract:
- Triggers: what event or state change starts it.
- Content: what information is sent to Neuro, in which message type, with what wording and level of detail.
- Timing: cooldowns, intervals, debouncing, priority between competing messages.
- Suppression: when it stays quiet (combat, menus, an action window or force active, Neuro mid-action, loading screens).
- Interaction with actions and action windows.
- The reasoning behind each choice, where the code or comments reveal it. Label inferred reasoning as inferred.
- Skyrim-specific parts (script layer, events, forms) that will not transfer.

## 2. Check the contract
Confirm in the neuro-sdk repo which messages and fields exist for this (context, actions, force). If Skyrim does something the API doesn't define, say so. Follow the one-active-force rule in CLAUDE.md.

## 3. Map to this codebase
Read the relevant `.claude/rules/*` and the neuro-fnv-actions skill if actions are involved. Identify:
- which existing modules own the relevant state, scheduling and sending;
- existing helpers to reuse (including in `src/Inventory/`, `src/Combat/`, `src/Menus/`);
- the engine-free policy part (host-testable) vs. the engine-touching part.

## 4. Find FNV-side equivalents
For each Skyrim mechanism that needs engine data or events: search the project, NVSE/JG definitions, then reference plugins and menu extracts (docs/reference-research.md). Never invent addresses, offsets, menu IDs or event names. Record source-backed facts separately from assumptions.

## 5. Write `docs/design/<feature>.md`
Sections: Goal, Skyrim behavior (with the why), What transfers, What doesn't, Proposed design (modules touched, new types, hook points), Engine-free logic and tests, Unverified assumptions, Manual in-game checklist, Open questions.

## 6. Stop and wait
Present the note and ask for approval before writing code. After approval, implement on a branch, add host tests for the engine-free logic, format with clang-format, build with `just debug`, and report per CLAUDE.md (evidence, checks run, unresolved assumptions, manual checklist).

Do not: copy Skyrim code or structure, add a second scheduler/sender beside the existing one, or claim runtime behavior from compilation alone.
