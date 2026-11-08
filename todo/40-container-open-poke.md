# 40 - Opening a container leaves Neuro idle

Status: in progress (container part implemented, to verify in game; Pip-Boy inventory still open)
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
