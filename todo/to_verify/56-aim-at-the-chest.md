# 56 - Shoot actors in the chest, not at a guessed height

Status: to verify
Area: Aim / Combat (`src/utils/AimUtils.*`, `src/Movement/AimLogic.*`, `src/CombatHandler.cpp`)
Source: designer's play-testing, 2026-11-12
Related: `archive/27-aim-unification.md`, `archive/03-attack-object-aim.md` (objects already use their mesh bound),
`47-auto-aim-down-sights.md`

## Problem
"It currently just shoots enemies at their feet. There should be a better position to be shot at, preferably at their
center chest. This is particularly important for small enemies like the geckos, but even humans are barely getting
hit."

Shots at an actor were aimed at its origin (the feet) plus a fixed height: 70 units for a character, 35 for a
creature, times the scale. Both numbers were estimates left over from before the aim unification (todo 27 lists them
as "the next candidates for real data"). 70 is about the belt of a human, so half of a hip-fire spread goes into the
legs and the ground, and 35 has nothing to do with the shape of any particular creature.

## What the game itself knows
Every actor base has body part data, and its torso part names the skeleton node VATS aims at for a torso shot.

- Layout: `BGSBodyPartData::bodyParts[15]`, indexed by part type with the torso at 0; each `BGSBodyPart` has
  `VATSTarget` and `partNode` (JG `nvse/GameForms.h`). JIP-LN-NVSE's `GetBodyPartVATSTarget` reads
  `GetActorBase()->GetBodyPartData()->bodyParts[part]->VATSTarget`.
- Checked in `FalloutNV.exe` 1.4.0.525: `TESActorBase::GetBodyPartData` is vtable slot 0x60. `TESCreature`'s
  (0x5FA2E0) returns its own data at +0x144, or the default record. `TESNPC`'s (0x5F0F80) returns the player's record
  for the player's base and the default record for every other NPC.
- Checked in `FalloutNV.esm` (all 49 `BPTD` records, the part with type 0): the torso's VATS target is `Bip01 Spine2`
  in `DefaultBodyPartData` (every human and ghoul) and for geckos, dogs, mole rats, giant rats, bighorners and
  deathclaws; `Bip01 Spine1` for super mutants, brahmin and night stalkers; `Bip01 Spine` for ants, radscorpions,
  mirelurks, bloatflies and most robots. A few are odd: cazadores `Bip01 Root`, turrets `Bip01 Pitch`, the eyebot
  `Bip01 Head`, the ZAX eye a file path, and Mr. House has no torso part.
- Finding the node: `BSUtilities::GetObjectByName(scene, name)` at 0x4AAE30 (JG; itr-nvse reads head positions
  through the same address). In the exe it forwards to 0xC4B310, which returns null for a null scene or name.

## Result (2026-11-12)
Built, host-tested and compiled; not run in the game.

- `AimUtils::ActorTorsoPoint(ref, point, source)`: the world position of the torso's VATS target node in the actor's
  loaded skeleton, or of the torso's part node when the target node is not there.
- `AimUtils::TargetPoint(ref, Shoot)` for an actor is now, best first: that torso point, then the centre of the
  actor's mesh bound (the same reading objects use), then the actor's origin. The 70 and 35 are deleted, with no
  fallback to them (designer, 2026-11-07: a real value or nothing). The origin case means the actor has no loaded
  body at all; the log names it `ORIGIN (no usable point)` so it gets reported.
- `AimLogic::UsableBodyPoint`: a node only counts when it is finite and within 300 units times the actor's scale of
  the actor's origin, so a stale skeleton cannot throw the aim away (host-tested in `tests/AimLogicTests.cpp`).
- Everything that shoots uses it: the engagement's aim, the line-of-sight ray (`attack` validation, `query_enemies`,
  the engagement), and the turn toward a target before VATS opens.
- The `Aim <name>:` log line is now written for actors too (once a second during an engagement) and says where the
  aim point came from: `aim point z+94.2 from the torso target node`.
- Walker's look height for actors (100, where the view rests when walking up to someone) is unchanged; it is not a
  shot.

## Not done, next candidates
- The firing tolerance for actors is still 0.05 rad heading and 0.07 rad pitch plus the settle check. At 10 m that is
  35 and 49 units. With the aim point now on the chest a shot inside the tolerance lands on the body more often than
  before, but there is still no real number for how wide a torso is. If humans are still missed after this change,
  the tolerance and a moving target's lag are the next things to look at; the `Aim` lines will show the errors.
- No lead for a moving target (designer's decision in todo 27, unchanged).
- Aiming down sights (todo 47) would tighten the spread.

## Unverified
- That the torso node's world position is up to date for an actor that is far away or off screen. The 300-unit check
  catches a skeleton left far behind; a small lag would not be caught.
- Which way an actor lying down or knocked over is hit: the node follows the body, so it should be right.
- The odd records: a cazador is aimed at its root node, a turret at its pitch node.
- That the mesh bound of an actor is a usable second choice (it is only reached when both nodes fail).

## Manual in-game checks
1. `attack` a gecko and a human (Powder Ganger) at about 10 m with a pistol: the shots land on the chest. Send the
   `Aim <name>:` lines; expect `from the torso target node`, with `aim point z+` around 90 to 100 for a standing
   human and well under that for a gecko.
2. The same at about 3 m and at 25 m.
3. A crouching or sitting NPC, and one that has been knocked down: the aim follows the body.
4. An ant, a radscorpion, a robot or a turret if one is at hand: the aim is on the body. Report the name with any
   line that says `torso part node`, `mesh bound centre` or `ORIGIN`.
5. `query_enemies` with enemies in plain view: "clear line of sight" for them. An enemy whose legs are behind a low
   wall but whose chest is visible now counts as in view.
6. `open_vats` with an `enemy_id`: the view turns onto the enemy's chest before VATS opens.
7. Bottles (`attack` with `object_id`) still get hit: the object path is unchanged, the line now says `from the
   object bound centre`.
8. If humans are still missed: send the `Aim` lines with the heading and pitch errors at the moment of the shots, and
   say where the shots go.
