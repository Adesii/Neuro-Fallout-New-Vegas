# 49 - Fix JSON string escaping upstream in libneurosdk

Status: open (later; the plugin works around it)
Area: `libs/libneurosdk` submodule (`src/neurosdk.c`), upstream https://github.com/Adesii/libneurosdk
Source: designer, 2026-11-08, after the "Invalid escape character x" rejection of a container listing
Related: commit 97d7830 (the plugin-side workaround, `src/Context/WireText.*`), `40-container-open-poke.md`

## Problem
libneurosdk builds its JSON by hand and two parts of it produce invalid JSON (pinned at `355a4a4`):
1. `escape_string` (`src/neurosdk.c`, about line 142) writes every byte below 32 or above 126, other than `\n`, `\r`
   and `\t`, as `\xNN`. JSON has no `\x` escape, so the server drops the whole message ("Failed to parse JSON:
   SyntaxError: JSON Parse error: Invalid escape character x"). This hits every non-ASCII character, even valid UTF-8:
   each byte of a multi-byte character becomes its own `\xNN`.
2. The actions/force `query` is put into the JSON with `"query":"%s"` as it is, without `escape_string`
   (about line 960), so a quote, backslash or newline in a query breaks the message.
   The game name at context creation, context messages, action names and descriptions, force state and action result
   messages all go through `escape_string`; schemas are passed in as ready JSON.

The plugin works around both: everything it sends is folded to ASCII (`WireText::ToAscii`) and the force query is
JSON-escaped first (`WireText::ToJsonStringBody`). See the note in `.claude/skills/neuro-fnv-actions/SKILL.md`.

## Fix (upstream, in the fork)
1. `escape_string`: copy bytes >= 0x80 through unchanged (the caller supplies UTF-8), write control characters
   below 0x20 as `\u00XX` (keep the short forms `\n`, `\r`, `\t`, and add `\b` and `\f`), and leave 0x7F as it is or
   write it as `\u007f`. The output buffer stays `len * 6 + 1` at most (`\u00XX` is six bytes; today it is `len * 4`).
2. Escape the force `query` with `escape_string` like `state`.
3. Optionally reject or replace invalid UTF-8 in `escape_string`, so a caller passing raw Windows-1252 gets an error or
   U+FFFD instead of a message the server rejects for another reason.
4. A test or example in the library that sends a context with a quote, a backslash, a control character and a
   multi-byte UTF-8 character, and checks the JSON parses.

## Then in this repo
- Bump the submodule (and `SDK_JSON_SHA256` in `tools/lib/env.sh` if `vendor/json.h` changed, per CLAUDE.md).
- Decide what stays of the workaround. The game's own strings are Windows-1252, so they still need converting: to
  UTF-8 instead of ASCII (keeps the real characters, "Tæles" stays "Tæles") or keep folding to ASCII (simpler for Neuro
  and the name search). Drop `ToJsonStringBody` for the query, or it gets escaped twice.
- Update the SKILL.md note and `WireText.hpp`'s comment to the new state.

## Done when
The fork escapes all strings into valid JSON, the submodule is bumped, and a container holding "Tæles of Chivalrie"
sends its listing without a server error, with the workaround reduced to the Windows-1252 conversion.
