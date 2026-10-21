# 39 - Report all distances to Neuro in metres

Status: open
Area: Context formatting (all modules that print a distance)
Source: designer decision 2026-10-21; `docs/code-review.md` section A (message wording)

## Problem
Distances reach Neuro as raw game units ("2000 game units"). An LLM judges "2000 metres" far more reliably than
"2000 units". Current print sites:
- `GameplayHandler.cpp:360,373,765` (quest locations, door distance, `query_nearby`)
- `CombatHandler.cpp:385,761` (enemy brief, `query_enemies`)
- the range words ("very close / nearby / farther away") are defined twice with the same numbers
  (`kVeryCloseDistance` 450 / `kNearbyDistance` 1500 in `CombatHandler.cpp:44-45`, hard-coded in
  `GameplayHandler.cpp:763`). Designer: **drop them**; with metres the LLM judges distance itself.
- New messages planned in todos 25/26/28 (walk progress, failure reasons) must use the same formatter.

## Decisions (designer)
- Switch **everything** to metres, and convert the existing messages as part of this task.
- **Remove the range words** (`RangeWord`, `kVeryCloseDistance`, `kNearbyDistance`) rather than converting them.
- Show whole metres (designer's example was "3 meters"); a single decimal below ~10 m is the only open detail.

## Work
1. **Find the conversion factor from evidence.** Do not guess. Candidates to check in the reference sources
   (`docs/reference-research.md`): the commonly quoted Bethesda figure is about 70 units per metre (the Creation
   Engine games' 1 unit ~ 1.43 cm); check GECK documentation / UESP-style notes, a reference plugin that converts units
   (e.g. in `.reference-data/repos`), and confirm in game by measuring a known distance (the player's own height in the
   console / a door width). Record the source and the measurement in this file and in a code comment.
2. Engine-free helper `Units::ToMetres(float)` + `Units::FormatDistance(float units)` (whole metres; "about" prefix
   where it is an estimate). Host tests for rounding. No range-word helper.
3. Replace every print site above; keep internal logic, thresholds and tolerances in game units (do not convert
   `kInteractionDistance` and friends).
4. Update the action descriptions / schemas that mention units, if any (`grep -rn "unit" src/Actions`), and
   `docs/` text that quotes example messages.
5. Mention in `neuro-fnv-actions` skill (message conventions) that distances are metres.

## Open questions
- The units-per-metre factor: no source is confirmed yet (step 1 of the work). Settle it from a recorded source plus an
  in-game measurement before replacing any message.
- Decimals for very close things (e.g. "0.5 m", "3.5 m"), or round everything to whole metres? Default: whole metres,
  "less than 1 meter" under 1 m.

## Done when
No message sent to Neuro contains "units", all distances come from one formatter, the conversion factor is justified
by a recorded source and an in-game measurement, and the host tests cover the formatter.
