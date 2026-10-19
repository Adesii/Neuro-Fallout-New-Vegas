# 19 - Surgery and medical questionnaire menus

Status: open

Surgery menu (id 1054 `Surgery`, XML `surgerymenu.xml`; note `SurgeryMenu`/`BarberMenu` share 1036 with `RaceMenu`) and the character-creation medical questionnaire (id 1076, `MedicalQuestionnaire`). **Not stubbed**; surgery has a `close` control in the XML (`xbuttonb` -> `close`) that was not verified as a plain exit.

## Goal
Check which of these appear in normal play (doctors, the Goodsprings doctor intro/character creation), then give Neuro enough to
get through them (answer the questionnaire; treat or leave surgery).

## Notes
- The character editor (RaceMenu 1036) already has `CharacterEditorHandler`; check how surgery/barber reuse the same id before
  changing anything.

## Before starting
- Read `CLAUDE.md`; invoke the `neuro-fnv-actions` skill; read `docs/reference-research.md` (menu XML lives in the private
  extracts, see `.references`). Never invent control names, offsets or menu behaviour.
- This menu has **no stub** (see above): the game just pauses with no actions. Add a handler under `src/Menus/`, register it in `src/MenuHandler.cpp`, and make sure a way out (close/exit) exists first.
- Follow the menu-scoped `ActionWindow` pattern (see `SpecialAllocationHandler`, `ContainerHandler`), keep `close_*`
  available, and report what was verified versus assumed with a manual in-game checklist.
