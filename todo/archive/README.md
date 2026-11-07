# Archive

Tasks that are implemented, committed and **verified in game** by the designer. Verified so far: 01 `crouch_toggle`, 02 `stop_move` (2026-10-22); 05 `close_container` and the stub `close_*` actions, 07 `query_nearby` priority, 36 shared-utilities cleanup, 39 distances in metres (2026-11-07); 03 attacking objects, 27 aim unification, 23 background focus, 24 idle poke, 28 location and "you see", 30 context reliability, 31 menu window lifecycle, 33 action registry, 35 quest and object queries, 38 host tests, 46 query_nearby paging and search (2026-11-08; some checks waived by the designer, see each file).

Move a file here (with `git mv`) from `todo/to_verify/` only after the designer reports that it works in game. Set its
first line to `Status: archived (verified in game, YYYY-MM-DD)` and keep its `Result` section as the record of what was
built and how it was checked. Do not archive on the strength of a successful build or host tests.
