# 40 - Opening a container leaves Neuro idle

Status: in progress (container part implemented, to verify in game; 2026-11-09 follow-up open; Pip-Boy inventory still open)
Area: Context / Menus (`src/Menus/ContainerHandler.cpp`), related to todo 24
Source: designer's play-testing, 2026-10-25

## Problem
After a container opens, Neuro sits there idle forever. The listing is sent silent (`PublishItems` ends in
`SendContext(..., true)`), and `.claude/rules/container-inventory.md` says no container action force is issued, so
nothing prompts a response. The "Container actions: loot_item, query_items, ..." line is in a silent message too.
(2026-11-07: that line and all other action how-to text were removed from the listing; it is state only now.)

## Questions to settle with the designer
- Is the right fix a **non-silent opening message** (cheapest, matches the "never leave her with nothing" rule), a
  **force** with a low-priority `ActionWindow` over the six container actions, or a **menu-aware idle poke** from the
  todo 24 poker (container open counts as "waiting on her", not as busy)?
- Todo 24's poker is suppressed while a menu is open (verify in `archive/24-...`); a container is a menu that
  expects a decision, so it probably needs an exception either way.

## Work
1. Read `archive/24-idle-poke-and-wakeup.md` to see how the poker treats open menus; decide the approach above.
2. Make the opening message wake her (or force), keep the listing/binding behaviour as is.
3. Check the same for the other decision-expecting menus that publish silently (Pip-Boy inventory pages, dialogue is
   already forced).
4. Add a host test where the policy lives in an engine-free module.

## Done when
Opening a container in game makes Neuro act within a few seconds without anyone nudging her, with no repeated spam
while she works through the transfers.

## Result (2026-11-08, container part)

The designer reported again that Neuro does not react when a container opens (with Gary, which only answers
non-silent messages). Chosen without a separate design round, as the cheapest fix that also covers the later idle
moments:
- The listing sent when a container opens is now **non-silent**, once per open (`PublishItems(..., silent=false)` in
  `src/Menus/ContainerHandler.cpp`). Query results and transfer notes stay silent.
- While the container waits on her (`ContainerHandler::AwaitsDecision()`: it is the top menu, the listing went out,
  nothing is queued, retried or closing), the idle poker no longer counts the menu as busy (`Context::IsBusy`,
  host-tested in `tests/IdlePokerTests.cpp`). So after a query result or a finished transfer she gets the usual
  idle poke (6-10 s, spacing out to 30-60 s), and the post-action poke 1 s after a busy stretch she started ends. During
  transfers, a quantity prompt or closing it stays busy, so there is no poke while she works through transfers.

Not done: item 3. The Pip-Boy inventory has the same shape (its listings are silent and the Pip-Boy counts as busy),
but its execution state is more involved; it needs its own `AwaitsDecision` and a decision whether its opening should
be non-silent too.

Unverified: everything in game. The poke phrases are wasteland flavoured ("The wasteland waits...") and are not
container specific.

Manual in-game checks:
1. Have Neuro open a container (interact). Expected: the container listing arrives as a non-silent message and she
   acts on it (loot, query, close) without anyone nudging her.
2. Let her query items and then do nothing. Expected: an idle poke after about 6-10 s, later ones spaced out; none
   while a transfer or Take All runs, or while the quantity prompt is open.
3. Close the container. Expected: no poke spam during the close; normal gameplay pokes afterwards.
4. Open a container by hand while she is idle. Expected: the same non-silent listing.

## Follow-up (designer's play-testing, 2026-11-09): she does not realise a container is open

The non-silent opening and the menu-aware poke are in, but a new playtest showed the model still believed it was in
normal gameplay while the container menu was open. Two causes:

1. **The opening context is too slim.** It starts with `## Container contents: <title>` and the item page. It never
   says that a container has been opened or that she is now inside a menu. When she used `interact_with`/`move_to`
   toward the container, the Walker's "Reached X" / interaction messages arrive several seconds before the menu
   actually opens (she has to walk there first), so the two messages are far enough apart that nothing links them.
2. **No poke inside the container screen.** While the container waits on her the idle poker is allowed to fire
   (`AwaitsDecision`), but the generic wasteland phrases ("The wasteland waits...") say nothing about being in a menu.
   Either the poke did not fire at all or it fired and read like a gameplay nudge; the playtest cannot tell which.

### Work
1. Rewrite the opening context so it states the state change first, before the listing. Draft (designer to adjust):
   "## Container opened\nYou opened <title> and are now inside its menu, not in gameplay. Movement and world
   actions are unavailable until you close it." followed by the existing listing. Keep it state only, no action
   how-to text (see the 2026-11-07 removal in the Problem section). Say which container, and for a container that is
   empty or locked say so in the same message.
2. Give the container screen its own poke text. A poke that fires while `ContainerHandler::AwaitsDecision()` is true
   must name the situation ("You are still inside <title>. Take what you want or close it."), not the generic
   gameplay phrase. Prefer an engine-free hook in `src/Context/` (poke text chosen by a "waiting screen" input to
   the poker, host-tested in `tests/IdlePokerTests.cpp`); `ContainerHandler` supplies the screen name only.
   Draft the phrases and show them to the designer before coding (same rule as todo 24).
3. Find out why no poke was seen: read the poker's busy inputs against `AwaitsDecision()` (announce delay
   `kStepDelay`, `g_announced`, the post-action poke that was started by the walk to the container, the 6-10 s first
   interval) and log when the poker is suppressed by the open menu. Fix the cause, do not just add text.
4. Apply the same state-first opening to the other decision menus that already announce themselves (Pip-Boy
   inventory, stub menus, dialogue) as far as they lack a "you are now in X" line; check each message that opens a
   menu.
5. Remember the Pip-Boy part (item 3 of the original Work list) is still open; solve it together with this.

### Done when
Opening a container (also after a long walk to it) tells Neuro first that she is inside a container menu, and while
she idles in it she gets a poke that names the container screen, not a gameplay phrase.

### Manual in-game checks
1. `interact_with` a container several metres away and wait. Expected: "Reached/interacting" message, then later a
   clearly worded "container opened, you are inside its menu" message; she does not call gameplay actions.
2. Idle inside the container. Expected: a container-specific poke after about 6-10 s, spaced out afterwards.
3. Same for an empty container and for a container opened by hand.
