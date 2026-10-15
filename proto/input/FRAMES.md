# EGRESS input frame data

Implemented in `src/core/eg_input.c` (buttons) and `src/core/eg_move.c` (d-pad). Tests: `host/test_input.c`,
`host/test_move.c`. Frames count from 0 on the frame a move starts. Windows are inclusive.

## Buttons

| Move | Input | Startup | Active | Recovery | Total | Window |
|---|---|---|---|---|---|---|
| Dodge, roll | B press | 0-1 | 2-11 (i-frames) | 12-17 | 18 | B->A cancel 4-14 |
| Dodge, blink | B press | 0-2 | 3-6 (i-frames) | 7-13 | 14 | B->A cancel 6-11 |
| Dodge, slide | B press | 0 | 1-12 (i-frames) | 13-19 | 20 | B->A cancel 3-16 |
| Sprint | B held >= 11 frames | loop while B held | | | | starts at the dodge's first recovery frame, or at the threshold if that is later: roll 12, blink 10, slide 13 |
| Skid | B released in sprint | | | 0-3 | 4 | A or B cancels it at once |
| Lunge | A in the dodge cancel window, or A in sprint | 0-1 | 2-5 | 6-17 | 18 | combo link to Hit 2 from 10; keeps dodge momentum |
| Hit 1 | A press | 0-3 | 4-5 | 6-15 | 16 | combo link to Hit 2 from 8 |
| Hit 2 | A, buffered or in the link | 0-3 | 4-5 | 6-17 | 18 | combo link to Hit 3 from 9 |
| Hit 3 | A, buffered or in the link | 0-5 | 6-8 | 9-26 | 27 | finisher, no link |
| Charge | A held >= 11 frames | loop while A held | | | | branches off the hit at frame 10 (outside its active frames) |
| Charged | A released in charge | 0-2 | 3-6 | 7-20 | 21 | level 1: held 11-29, level 2: 30-49, level 3: 50+ |
| Parry | B within 0-4 frames of A's press, or while A is held | 0-1 | 2-9 (guard) | 10-21 | 22 | perfect 2-4 |

Rules:
- **Tap or hold:** the press frame counts as frame 1 of the hold. Released after 10 or fewer frames is a tap.
  Held for 11 or more is a hold (`EG_HOLD_T = 11`). Taps never wait: B starts the dodge and A starts Hit 1 on the
  press frame. The hold branches off that move afterwards.
- **Buffer:** one slot, `EG_BUF_FRAMES = 6`. A press can fire on its own frame or on any of the 5 frames after it.
  The newest press replaces the old one. A press the state can't take yet waits in the slot, for example A during
  attack startup or active frames, or B during an attack's active frames, a dodge, or a parry.
- **A before the cancel window:** buffered into the window. It fires on the window's first frame, which is always
  within 6 frames of a press made after the dodge started. A after the window but before the dodge ends is a normal
  A: Hit 1 starts on the first idle frame if the buffer hasn't expired. A after the dodge ends is a normal A.
- **A then B:** B counts as the secondary (parry) when A is held, or when A went down 0 to 4 frames earlier
  (`EG_AB_WIN`). Otherwise B is a dodge. The parry can cancel any phase of Hits 1-3 and the charge, but only the
  recovery of the lunge and the charged attack.
- **Same-frame A+B:** parry. This is the 0-frame end of the A->B window. B->A can't mean the lunge on that frame,
  since every cancel window opens at dodge frame 3 or later. Plain B already gives the dodge, so the chord gets a
  meaning of its own.
- **B during attacks:** a dodge cancels a hit's startup and recovery at once. During active frames it is buffered.
  For the lunge and the charged attack, a dodge only cancels recovery.
- **No infinite cancels:** a dodge started out of a lunge is marked as chained and can't lunge again. A in its
  window is held as a normal A. Hit 3, the charged attack, the parry and dodges can't be cancelled by A.
  Counters saturate at 255.

## B hold options

1. **Sprint (implemented):** if B is still held at the threshold, the dodge's recovery is replaced by a sprint that
   lasts while B is held. A during the sprint is the lunge. Releasing B gives a 4-frame skid that A or B can cancel.
   This keeps the dodge-first feel and suits a grid crawler built around getting through rooms.
2. **Held guard:** the dodge ends early into a block stance while B stays held. It was rejected because it overlaps
   A->B parry and makes B mean two opposite things, evading and standing still.

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
