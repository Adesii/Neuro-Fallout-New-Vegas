# Combat and VATS

Scope: `CombatHandler` (world combat), `Menus::VatsHandler` (the VATS menu), `src/Combat/CombatLogic.*` (engine-free
rules, covered by `tests/CombatTests.cpp`), `src/Actions/Combat/`. Cloud cannot run FNV, so everything below the
"Evidence" heading is source- or binary-backed, and everything under "Unverified" needs the in-game checklist.

## Design (confirmed with the designer)

- `attack`, `stop_attack`, `query_enemies`, `open_vats` are persistent gameplay actions. `attack` is available during
  all gameplay (shooting a bottle can be a quest step) and never becomes unavailable while an engagement runs.
  `stop_attack` stays registered while a menu is open, because an engagement pauses across menus instead of ending.
- An engagement also ends with a "not hurting it" message after 15 s of attacking a reachable actor whose health never
  drops (essential, invulnerable or over-armoured targets), and when a thrown weapon is used up and the hand empties.
- `attack` takes exactly one of `enemy_id` (from `query_enemies`) or `object_id` (from `query_nearby`). It engages
  until the target dies, ammo runs out, line of sight is lost for 8 s, or `stop_attack`. Calling it again retargets.
  Neuro may walk, switch weapons or open the Pip-Boy meanwhile; combat keeps aim ownership (Walker look is suppressed)
  while Walker keeps moving the player, so walking while shooting strafes.
- Melee weapons close in through Walker (`Owner::Combat`, only when no other Walker command is running); ranged weapons
  ignore distance. Line of sight is required to accept `attack`, not only during it.
- Only explicitly friendly actors (faction relation Ally or Friend, or a teammate, and not fighting the player) are
  excluded from `query_enemies`, `attack` and VATS. Neutrals are listed and attackable: that is Neuro's choice. A
  neutral whose Aggression is Very Aggressive or Frenzied (GECK order 0-3, read from `GECK.exe`) is only *labelled*
  hostile and sorted first, since it attacks on sight; the label never affects eligibility.
- Non-actor `object_id` targets are free-aim: a bounded burst (5 shots, 1.5 s automatic, 3 swings), then a stop
  message, because objects have no health to observe.
- Combat context is event-driven and non-silent: combat start/end, health crossing a band (75/50/25/10%), radiation
  stages (every 200 rads), weapon change, empty magazine / no ammo, kills, and the reason every engagement stopped.
  The start message lists the hostile enemies with their ids (refreshed at that moment, so no query round trip) and the
  player status, nothing more. Updates are batched at most every 3 s. `query_enemies` results are silent, like `query_nearby`.
- Damage and death (todo 29, `src/Combat/DamageLogic.*`): hits on the player become one note per attacker per 10 s
  ("X hits you with W.", batched into the 3 s combat update); health bands and radiation stages are also reported
  *outside* combat as a "Status update" (falls, radiation ticks, traps), weapon/ammo notes stay combat-only. Dying sends one
  non-silent "You died" message: the fact that the save reloads, what killed her (and what she was doing) and one lesson
  line chosen from the cause, plain coaching most of the time and a playful roast about one death in four (seeded, only
  where she had the information: low health she was told about, radiation stages, a fight she picked).
- VATS is a menu-scoped action window (`vats_queue_shot`, `vats_execute` once a shot is queued, `vats_cancel`) with a
  Medium-priority force. Walker, engagement and world actions pause for the whole of VATS including playback.

## Evidence

- Hostility: `Actor::GetFactionRelation(target, bool *isEnemy)` is `FalloutNV.exe` 1.4.0.525 `0x8B87A0`, `__thiscall`,
  `ret 8`. Disassembly of the call in `Actor::AttackedBy` at `0x898A54` shows `(this=actor, [0x11DEA3C]=player,
  &flag)`; the combiner at `0x8B8740` orders results Neutral 0 / Enemy 1 / Ally 2 / Friend 3. itr-nvse
  `features/AggroThreshold.cpp` calls the same site. Binary checked: `FalloutNV.exe` (not `_backup`).
- Fighting the player: `Actor::isInCombat` (0x104), `combatTarget` (0x128), `combatTargets` (0x12C), as read by
  JohnnyGuitarNVSE `IsCombatTarget`. Player combat flag: `PlayerCharacter::pcInCombat` (0xDF0).
- Loaded ammo: `MiddleHighProcess::ammoInfo` (0x118) `->iNumber`, weapon drawn: `isWeaponOut` (0x135), process level
  at BaseProcess 0x28 (JIP-LN-NVSE `Actor::GetAmmoInfo`). Reserve ammo is scraped from the HUD `tile058` text
  `loaded/reserve`, as FalloutNVAccess `HUDMainMenuHandler.cpp` does; it is unknown when the HUD text is absent.
- Input: mouse bindings are the DirectInput button index in `OSInputGlobals::mouseBinds`, hooked at index 256 + n
  (`src/hooks`); `src/utils/InputUtils` resolves the Attack control that way, keyboard otherwise.
- Line of sight: a Havok ray on layer 37 (`LAYER_LINEOFSIGHT`) via `TES::PickObject`, copied from
  `src/itr/NavmeshCommands.cpp`, plus the crosshair actor (`PlayerCharacter::reticleActor`, 0xD44) resting on the target.
- VATS globals and layouts: see the header of `src/defs/VatsMenu.hpp` (menu 0x11DB0D4, target 0x11F21CC, mode
  0x11F2258, target list 0x11DB150), cross-checked between FalloutNVAccess and itr-nvse. `vats_menu.xml` (private
  extracts) gives `GlowBranch/accept_button` (id 4), `return_button` (5) and the PC-visible `left_arrow` / `right_arrow`
  (ids 2 and 3).

- Hits: NVSE's built-in `OnHit` event through `NVSEEventManagerInterface::SetNativeEventHandler` (JG `PluginAPI.h`);
  xNVSE `EventManager.cpp` `HandleGameEvent` / `EventCallback::Invoke` hand the native handler `{victim, attacker}`
  (`s_lastOnHitVictim = source; s_lastOnHitAttacker = object`). Registered on `kMessage_DeferredInit`.
- Killer: `Actor::killer` at 0xC0 (JG `GameObjects.h`), read only when the pointer is one of the loaded references.

## Unverified, needs the game

1. Whether line of sight works (the ray now lives in `WorldUtils::HasLineOfSight`, shared with Walker): the ray start is moved 35 units past the player and ends 60 units before the target to
   avoid self/target collision. If `query_enemies` reports "no line of sight" for visible enemies, this is why. The
   `Combat line of sight ... changed` debug lines in the log show every transition.
2. Melee reach: 100 units, scaled by `weapon->reach` when above 1. The real engine reach is not read.
3. Ready-weapon control: one tap draws a holstered weapon and reloads a drawn one (stock FNV behaviour assumed).
4. VATS keys (designer's current best guess, to be corrected): W/S cycle targets, A/D cycle body parts, LMB queues,
   the accept button confirms, the return button leaves. Body-part cycling falls back to clicking the right arrow tile
   after the keys fail. Each step verifies engine state (target ref, current body part, queue count / AP) and aborts
   with a message instead of looping.
5. Whether VATS opens at all with no enemy in view, and whether mode returns to 0 after playback. Playback blocks
   gameplay for at most 90 s (watchdog).
6. Semi-automatic fire uses 0.1 s trigger pulses; real fire-rate limits may make some weapons feel slow.
7. "Explicitly friendly" is implemented as faction relation Ally **or** Friend, **or** a teammate. If a town faction is
   Friend to the player, its members are excluded from enemies/attack/VATS and Neuro cannot choose to attack them.
8. The stall timer (20 s) no longer runs while combat's own melee approach is closing in (the distance must shrink by
   20 units per second, measured in 1 s windows); a blocked approach that gets no closer still ends the fight as
   `Stalled` after 20 s. The 20 units/s and 1 s are guesses; Walker's own progress/stuck failures (todo 25) report a walk
   that cannot get there.
9. With the reserve unknown (HUD ammo text missing: `reserve == -1`), a magazine that stays at 0 for 6 s counts as out of
   ammo. A real reload takes about 2-3 s, so 6 s should not misfire; verify with a slow reload weapon.
10. The first shot goes out on the first frame the weapon is ready (the pulse timer starts "ready" and is reset to ready
    whenever the weapon cannot fire). Whether a game-side fire-rate limit or draw animation makes that first press
    ineffective is unknown.

## Manual in-game checklist

Setup: Goodsprings, a ranged weapon with ammo, a melee weapon, a neutral NPC, a hostile (geckos/ants).

1. `query_enemies` near neutrals only: they are listed as neutral, none is a companion/ally. With a hostile in view:
   hostile first, line-of-sight flag true; behind a wall: "no line of sight".
2. `attack` a hostile with a pistol: weapon is drawn if holstered, aim turns, shooting, stops when dead with a
   "dead" message. Repeat with an automatic weapon (trigger held) and a revolver (pulsed).
3. `attack` with no line of sight is rejected with the correct message; step behind cover mid-fight: shooting holds,
   resumes when visible, and ends with a line-of-sight message after about 8 s.
4. Empty the magazine: reload happens by itself; run out of reserve: out-of-ammo stop message.
5. Melee weapon: `attack` walks up to the target and swings only when close; `move_to_object` elsewhere mid-fight
   moves her without cancelling the engagement; `stop_attack` ends it and Walker stops.
6. `attack` a second enemy while engaged: retargets. Open the Pip-Boy mid-fight and equip another weapon: the engagement
   resumes after closing; no mouse button stays stuck in any case (also test disconnect and loading a save mid-fight).
7. `attack` a bottle by `object_id`: short burst, then a stop message; the log gets `Aim <name>:` lines (todo 03 / 27 measurements). An explicit ally/companion is rejected from both
   `enemy_id` and `object_id`.
8. Combat start/end messages and updates (health bands, rads, empty magazine, kills) arrive once each, not per frame.
9. `open_vats`, with and without `enemy_id`: menu opens, window lists targets and body parts with hit chances.
   `vats_queue_shot` selects target and part visibly, `vats_execute` plays the sequence, a finished message follows,
   Walker and combat resume. `vats_cancel` leaves. If target or body-part cycling fails, note which inputs worked and
   send them back so the bindings in `VatsHandler.cpp` can be corrected.
10. First-shot latency: `attack` with a pistol drawn and the target in view: the shot fires as soon as the aim lines up, not
    after an extra beat; same for a thrown grenade and a melee swing once in reach.
11. Long melee approach: `attack` an enemy 100+ m away with a melee weapon: she runs up without the engagement ending
    as "could not make progress" after 20 s; block the way (stand behind a closed gate): it ends as stalled after ~20 s.
12. Failure symptoms to report: stuck mouse button, permanent Walker lockout after VATS (check the 90 s watchdog log
    line), `Combat engagement ended` log lines with unexpected reasons, VATS refusing to open.

## Damage and death: unverified and checklist (todo 29)

Unverified:
- That the native `OnHit` handler fires for hits on the player and that its arguments are `{victim, attacker}` in that
  order (source-backed in xNVSE, never run here); that the attacker is null (or not an actor) for falls and fire.
- That `Actor::killer` is set when the player dies. It is only a first candidate; the last hit within 8 s and radiation
  at 900+ rads (the 1000-rad lethal level is not source-verified here) are the fallbacks, then a "sudden loss" best guess.
- The order of the death message against the game's own save reload (`PreLoadGame` resets the state). The message is
  sent from the frame loop the first frame the player is dead, whatever menu is open.
- Whether the lesson tone (coaching vs roast) reads well on stream; the lines are in `DamageLogic.cpp`.

In-game checks:
1. Get shot by one enemy for 15 s: a "Combat update" with at most one "X hits you with W." per 10 s; two enemies each
   get their own note.
2. Fall from a height and survive: a "Status update" (not "Combat update") appears when health crosses a band; no weapon or
   ammo notes appear outside combat.
3. Stand in a radiation source: "Your radiation exposure rose..." appears per 200 rads, also outside combat.
4. Step on a mine or a trap: a note "You were hurt by ..." and, if it kills, the death text names a non-creature source.
5. Die in a fight at low health: one "You died" message naming the killer, the weapon and what you were doing, then the
   save reloads; no second death message after the reload. Check `neuro-fnv.log` for the `Player died:` line.
6. Die from a fall and from radiation (`player.modav radlevel 1000`): the cause text matches.
7. Start a fight: the "Combat started" message has the hostile list and your status, no usage paragraph.
8. Failure symptoms: no OnHit log line ("Listening for OnHit events"), hit notes never appearing, a death message
   arriving twice, or none.
