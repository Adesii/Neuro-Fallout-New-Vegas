# 44 - Debug camera: free camera or top-down view while the Walker runs

Status: open
Area: tooling (camera, input hook)
Source: designer's play-testing, 2026-11-07
Related: `43-walker-debug-path-overlay.md`

## Problem
The path overlay from todo 43 is hard to read from the player's own view: the route ahead hides behind the obstacle
that is the problem. The designer wants to watch the route from outside while Neuro's character keeps walking.

## Goal
A debug camera the designer can switch on in game: a free camera, a top-down camera above the player, or both. The
Walker, combat and Neuro's actions keep running underneath it, and the camera itself never moves the player.

## Notes
- Vanilla has the `tfc` console command (toggle free camera). Try it first with a Walker command running: does the
  player keep walking, and does the hook's injected input still reach the player and not the camera? Not tested.
- The Walker writes the player's yaw and pitch (`AimUtils::SetYaw`/`SetPitch`) and steers with movement keys relative
  to that view. A camera that changes the player's view or takes over the movement keys breaks it. A custom camera
  must leave the player's rotation alone.
- A top-down camera that follows the player, looking straight down from a set height (adjustable), would show the
  whole route from the first node on. It needs the scene camera position and rotation. Research first (JG/NVSE
  definitions, `docs/reference-research.md`), with no invented offsets. No reference plugin in `.reference-data/repos`
  was found with a free camera.
- Real mouse and keyboard input for a free camera must not leak into the player while debug is on, or the designer
  could not move the camera without steering the character.

## Questions for the designer
- Is vanilla `tfc` enough if the Walker keeps working under it, or is a follow-the-player top-down view wanted anyway?
- Toggle by the same console command as todo 43, or a separate one?

## Done when
The designer can watch a Walker route from above or freely while the walk continues normally, and switching it off
restores the normal camera with no input left held.
