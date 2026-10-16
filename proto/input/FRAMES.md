# EGRESS controls, 2026-10-15

This replaces the former B-to-dodge proposal following the owner's explicit correction.

| Input | Action |
|---|---|
| A tap | Primary attack; repeated taps continue the three-hit chain |
| A hold, release | Charge, then the heir's charged attack |
| B tap | Secondary attack: shot for blade/whip/launcher, impact/pulse for hammer/gauntlet |
| B hold | Repeat secondary attacks at the inherited weapon's cadence |
| A then B | Counter/parry combo, including a perfect return window |
| B then A | Rushing strike combo |
| Same-frame A+B | Counter/parry |
| Direction twice | Evade in that direction; relative forward uses the heir's roll/blink/slide |
| Left/right twice relative to aim | Sidestep, keeping the pre-tap aim |
| Back twice relative to aim | Backstep, keeping the pre-tap aim |
| Continue holding after evade | Run |
| Hold direction without a double-tap | Run after 18 frames of walking |

A directional double-tap needs a release between presses and at most 14 frames between press edges.
The aim before the first tap defines forward/side/back, even though the first walk tap turns the character.
Side/back evades last 12 frames; forward roll/blink/slide last 18/14/20. A 20-frame evade cooldown prevents
repeated taps cancelling every recovery. Action press buffers last six frames; ordered A/B combos use a
four-frame order window or the other held button. B alone never requests movement.

The shared primary attack timings remain blade 16, hammer 25, whip 19, gauntlet 10, launcher 22 frames
for the first hit. Held B repeats every 11/16/12/10/7 frames respectively. Walking/running can accompany
secondary fire. The bench continues to use A to select/commit, B to undo/close, START to use the selected tool.

## D-pad (eg_move)

- **Positions:** 12.4 fixed point, 16 sub-pixels per pixel. The hitbox is 12x12 px and grid cells are 16x16 px.
  Cells outside the grid count as walls.
- **Diagonal step:** speed x 181/256 (0.707). The 1/256 remainder carries from frame to frame, so N diagonal frames
  move exactly floor(N x speed x 181 / 256) on each axis. At speed 24, 32 frames move 543 sub-pixels diagonally
  against 768 straight.
- **4-way mode:** the most recently pressed held direction wins. When several directions go down on the same frame,
  they are ranked DOWN, UP, LEFT, RIGHT, so a horizontal press wins and right beats left. In 8-way mode, opposite
  directions cancel.
- **Collision:** the X move is resolved first, then Y, each snapping flush against the wall. This is what lets a
  diagonal slide along a wall at the diagonal speed.
- **Corner nudge:** applies to a cardinal move that is blocked while the box straddles two lanes and only one lane
  is solid. If the box overlaps the solid lane by 4 px or less, it shifts across toward the open lane, by up to the
  speed per frame and never past the lane edge.
- **Push:** a request is raised once, on the 10th consecutive frame (`EG_PUSH_HOLD`) of blocked contact with the
  same block cell, in the same direction, with that axis held. The pushed cell is the block in the lane that holds
  the box centre. On a diagonal, the push goes along the blocked axis while the other axis keeps sliding. If both
  axes are blocked, X wins. Walls never raise a request. The game moves the block.
