# 17 - Casino menus (slot machine, roulette, blackjack)

Status: open

Menu ids 1080 (`SlotMachine`, `slot_machine_menu.xml`, `SMM_Spin`/`SMM_IncreaseBet`/`SMM_DecreaseBet`/`SMM_ExitButton`), 1082 (`Roulette`, `roulette_menu.xml`, `ROM_PlaceBet`/`ROM_IncreaseBet`/`ROM_DecreaseBet`/`ROM_ExitButton`) and 1081 (`BlackJack`, `black_jack_menu.xml`). Slot machine and roulette are stubbed with `close_*`; **blackjack is not stubbed** because `BJM_Stay_Exit` doubles as Stay and Exit.

## Goal
Let Neuro bet and play: set the bet, spin/place a bet, play hands (hit, stay, double, split), and leave when appropriate; report
the result and caps after each round.

## Notes
- Blackjack needs the state to decide whether the button exits or stays; provide a close action only when it truly exits.
- Consider a context cap/time limit so she does not sit there forever (designer's call).

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- Slot machine/roulette (17) and tutorial/book (21) have a stub in `src/Menus/StubMenuHandler.cpp` (only a `close_*` action and a "not implemented" message). Replace each with its own handler and **delete its table row**; the others listed above have no stub yet (blackjack, SPECIAL book). Register new handlers in `src/MenuHandler.cpp`.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
