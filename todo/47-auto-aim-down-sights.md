# 47 - Aim down sights automatically for targets past 10 meters

Status: open
Area: Combat (`src/CombatHandler.cpp`, `src/Combat/CombatLogic.*`, input injection)
Source: designer, 2026-11-08
Related: `03-attack-object-aim.md` (aim from the camera), `27-aim-unification.md`

## Problem
There is no action to aim down sights, and combat never does it, so Neuro always fires from the hip. At range that
spreads shots for no reason. The designer aimed down sights by hand during the bottle test, so the effect on the aim
code is unknown too.

## Goal
While Combat is engaged with a ranged weapon and the target is more than 10 m away (`Units` meters, threshold kept in
game units in code), hold the aim-down-sights control. Release it when the target comes closer than that, the weapon
cannot use sights, and on every exit path of the engagement.

## Notes
- Engine-free decision in `CombatLogic` (weapon kind, distance, hysteresis so it does not flicker around 10 m, a short
  delay after the shot lines up), with host tests. `CombatHandler` only applies it.
- The control: resolve the bound Aim/Block control through `InputUtils::ResolveControl` (it is usually the right mouse
  button). Find the `ControlCode` for it in the project definitions first, not a guessed key. Hold it with its own
  `InputCore::Owner` bit so it never cancels the player's own press or another owner's.
- Release on pause, menus, VATS, retarget to a melee target, out of ammo, reload, death, load and disconnect
  (`.claude/rules/combat.md`: every exit path releases held inputs).
- Which weapons: ranged guns only. Not melee, unarmed, thrown or placed explosives. Check scoped weapons: the scope
  overlay changes the FOV a lot. Does the aim maths still converge, and does the camera position move?
- The aim and line-of-sight code read the rendered camera (`AimUtils::CameraPosition`). Check in the log that aiming
  down sights does not move it in a way that breaks the pitch or the line-of-sight ray. This is also a suspect in
  todo 03's line-of-sight refusals.
- Should Neuro be told? Probably not every time; maybe one line in the "Combat started" or attack-start context
  ("You aim down your sights at it."). Ask the designer.

## Questions for the designer
- Exactly 10 m, or per weapon (pistols closer, rifles further)?
- Also for `attack` on objects (bottles), or only actors?

## Done when
At over 10 m with a gun, combat aims down sights, fires, and lets go of the control afterwards on every exit path, and
the host tests cover the decision.
