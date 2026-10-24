# 40 - Opening a container leaves Neuro idle

Status: open
Area: Context / Menus (`src/Menus/ContainerHandler.cpp`), related to todo 24
Source: designer's play-testing, 2026-10-25

## Problem
After a container opens, Neuro sits there idle forever. The listing is sent silent (`PublishItems` ends in
`SendContext(..., true)`), and `.claude/rules/container-inventory.md` says no container action force is issued, so
nothing prompts a response. The "Container actions: loot_item, query_items, ..." line is in a silent message too.

## Questions to settle with the designer
- Is the right fix a **non-silent opening message** (cheapest, matches the "never leave her with nothing" rule), a
  **force** with a low-priority `ActionWindow` over the six container actions, or a **menu-aware idle poke** from the
  todo 24 poker (container open counts as "waiting on her", not as busy)?
- Todo 24's poker is suppressed while a menu is open (verify in `to_verify/24-...`); a container is a menu that
  expects a decision, so it probably needs an exception either way.

## Work
1. Read `to_verify/24-idle-poke-and-wakeup.md` to see how the poker treats open menus; decide the approach above.
2. Make the opening message wake her (or force), keep the listing/binding behaviour as is.
3. Check the same for the other decision-expecting menus that publish silently (Pip-Boy inventory pages, dialogue is
   already forced).
4. Add a host test where the policy lives in an engine-free module.

## Done when
Opening a container in game makes Neuro act within a few seconds without anyone nudging her, with no repeated spam
while she works through the transfers.
