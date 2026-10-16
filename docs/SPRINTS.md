# Native game completion ledger

This tracks the complete build order in `redesign.md` section 18 and the owner's later gameplay corrections.
The public `f4df94b` cartridge includes the verified native feedback, island-edge and inherited-art corrections.
It does not complete this ledger.
An integrated prototype, a passing host simulation, a native build and a played feature are separate evidence.

| Milestone | Current state | Required before completion |
|---|---|---|
| Core, movement and rendering | Office checkpoint verified | Keep current two-action-button controls, full bodies and frame/OAM budgets through the full game |
| Puzzles and finite tools | Partial | Composed dependency motifs, varied topology and states, resource-feasible alternatives, wrong-spend branches and native replay |
| Shared action/object/mob matrix | Partial | Complete the authored interactions and readable activation, reaction, spent and destruction states; exercise both directions |
| Mobs, stages, biomes and full run | Partial | Native full-run layout and seed bank, all biomes, stage/obstacle combinations and their kill/exit guarantees |
| Heirs, lineage, saves, tree and stats | Partial | Non-overlapping migrated SRAM journals, floor/landing continue, shortcut/line progress, full tree and stats pages with event accounting |
| Boss roster | Two office fights verified | Remaining biome bosses, final stretch and no-hit feasibility/frame budgets |
| Music | Existing score player | Native adaptive composition, transitions and in-key effects across the complete run, with captured/listened audio |
| Title and first-person stages | Title verified; renderer prototype | Playable generated first-person stages with the same tools/combat and measured timing |
| Balance | Host model and office checks | Full native run coverage, survival/resource/build variety gates and human play feedback |
| After the egress | Designed | Native descent, cycle/shortcut consequences, deeper truths and persistent records |

## Current work

The native feedback milestone now migrates FA1 families from A100/A300 into the reserved tree regions before
enabling continue writes. Floor-entry/landing journals preserve damage, inventory, the chosen heir and narration
history; interrupted end transactions finish once. This is a ten-floor office branch, still short of the full run.

No milestone will be called complete just because a checkpoint can be merged or published. Each completed entry
must point to its native implementation, focused evidence and relevant regression gates. The public ROM stays on
the tested checkpoint while this branch is under construction.

## Owner additions: larger stages and reactive narration

Stages must extend beyond the current small room. Camera travel must show the floor's tiles falling out of place
behind the player and assembling as they approach, with persistent puzzle/object state when returning. Merely
zooming the old room or adding empty floor does not satisfy the request. Validate travel, backtracking, object
state, enemy transitions and frame/VRAM budgets through the actual ROM.

Narration is driven by an event/history matrix: what the player did, consequences, close calls, repeated patterns,
family echoes and the narrator's changing confidence/confusion. Lines must remain grounded in occurred events,
have varied phrasing and a confused, wacky voice, and avoid repeating recent topics. No narration pauses play.
Text is much rarer than speech, short, deferred during pressure, and confined to the existing HUD footprint.
It must never grow the HUD over the field or interrupt attacks, movement or puzzle sight lines.

The branch includes eight fal speech prototypes, generated with the owner's authorized credential and the built-in
Rex voice. Actual cartridge events select them. They occupy 96,432 bytes at 8,192 Hz / 4 bit, with sixteen-byte
wave-RAM transfers at 256 Hz; no gameplay network requests. The shared 3,072-line grammar is host text, not 3,072
voiced clips. More causal/history combinations, voice approval and human listening remain.

## Further owner corrections (2026-10-15)

- Give the player real void falls from island edges and holes, a visible fall/recovery state and a directional
  forward roll-jump across short gaps. Grounded sidesteps/backsteps and running must not float across holes.
- Narrator reactions must follow actual hits, attacks, falls, successful crossings, puzzle actions and their
  consequences. Use the existing fal credential for narrator generation, explicitly authorized in chat.
- Replace the cramped heir picker: a first screen with all three heirs in matching framed cards, an arrow and a
  concise highlighted status; selecting opens a second screen with readable details, confirm and back.
- Compose switches, movable crates, falling CRTs, portals and finite tools into dependency chains with alternate
  resource-feasible solutions. The generator must prove those chains, rather than randomly decorating a room.
- B is now tap-to-shoot, with slower recovery. Holding B charges a distinct secondary and release executes it.
  This supersedes the earlier held-B repeat-fire request. Preserve A charge and both ordered A/B combos.
- Correct tile/feet alignment and collision masks so passage centers match their rendered openings.
- Teach pushing when the player first meets a movable object, with the heir visibly bracing/pushing, audible
  movement and contact particles. Add a real start menu and contextual tutorials; do not rely on hidden controls.
- Upgrade the bench into a small Crucible-like crafting scene with visible ingredients, their combination and
  the resulting object; inventory entries use small item icons and quantities.
- Pressure plates respond to player weight while occupied, and release when the player steps away.
- Detect genuinely irreversible pushed-box deadlocks and show the reset cue; retain alternative tool/jump routes.
- Add stamina with different action costs, roll costs, running drain, readable exhaustion and recovery; test balance.
- Align and validate attacks, visible arcs and aim in all eight directions.
- Keep the sitting idle, but repair its awkward seated pose using actual native state/direction comparisons.
- Apply interaction state coverage throughout: idle, anticipation, active, reaction, failure, spent and destruction,
  with fitting SFX, particles, subtle palette/dither variety and connected autotiled terrain/ground structures.

Settings/tutorial journal: AF68/AF78 (16 bytes each), from the explicitly free region after both tree copies.
This avoids both the 128-byte sky record at A240 and the legacy FA1 family source at A300. Settings writes must
not corrupt an old family before its migration. Core/native checks must cover mixed mapper saves and torn writes.

## Integrated feedback branch

Implemented in native code: the two-page heir picker; title lobby and controls pages; settings/tutorial journals;
tap/released-charge B; eight-way hit/arc/projectile alignment; distinct stamina costs and recovery; pit falls and
short forward roll-jumps; player-weight plates; reverse-push deadlock checks; braced pushing with contact dust;
the bench combination scene and sixteen inventory icons; connected carpet cards; repaired seated legs/hands.
These use the existing native renderer. The carpet's sixteen masks share sixteen quadrant tiles. Item icons use
sixteen more BG tiles, outside sprite VRAM. Floor-only skins alias picker-only scratch, with a tested stack canary.

The previous auto-run path cleared push contact every frame. Running into a crate now becomes a sustained brace.
BG commits defer on gravity ticks, late heir composition keeps the previous complete body for one tick, and the
clock carries minutes directly to avoid its former periodic 32-bit division hitch. Frame/OAM gates remain strict.

Still required: connected larger stages with camera-driven assembly/backtracking, conditional portals and composed dependency
graphs, the full native run/biomes/boss roster, complete tree/stats, adaptive music, first-person stages and cycles.
The tested 8 KiB WRAM cache is a standalone prerequisite; it is not yet a larger playable stage.

The full-run constructor can now stream into that cache with a 40-byte world header and one 12-byte scratch slot.
It matches 2,048 legacy layouts on the host and 24 actual native constructions byte for byte, including the first
52-floor run, shortcuts, cycles and descent, within the game's stack canary. See `validation/run-stream-20261016/`.
The playable full-run integration and its content are still required.

The feedback milestone was merged, pushed and published. Both public ROM downloads and the live browser's native
save migration were read back and verified; see `validation/feedback-20261015/site-release.json`. The physical
Chromatic has not been tested.

## Native island edges

The side and bottom silhouettes now use an explicit abyss cell. The same generated room can be walked off with
ordinary input: the heir visibly falls, pays one half-heart and returns to firm ground. Crates and CRTs can be
lost over the edge. Losing a required crate triggers the reset cue; a real alternative crafted weight still
prevents a false warning. Mud and dropped CRTs can fill shallow floor holes, while the exterior abyss stays open.
The falling-CRT switch motif now has a real pedestal instead of depending on an invisible boundary wall.

Attack centres can extend outside the map without wrapping onto a distant enemy, orb or living obstacle. Shots
expire before an out-of-map cell lookup. Shared push primitives reject wrapped row neighbours and invalid cells.
The solver and reset checker distinguish the abyss from fillable holes; the accepted room list is preserved.

Measured native optimization keeps the frame gates: shutter-free rooms skip the heavy plate pass, resource bars
use an exact five-bit ratio, and the HUD copies fixed rows in ROM0 without rewriting unchanged palette cells.
The bar's appearance is unchanged. The native no-hit player uses both attacks against the Fax, times its visible
ram for a perfect parry and avoids cancelling that guard with an unintended directional double tap. The original
zero-hit, all-phase, 120/120 active-frame and ten-sprites-per-line gates remain required.

This is still one 16 × 14 simulation grid per floor. The connected-stage architecture and its native completion
evidence are specified in `CONNECTED-STAGES.md`; opening the rim is not a larger-stage implementation.

The island-edge milestone was merged, pushed and published after its complete native gate. Both public mapper
images, the live browser and real older-save carryover were verified; see
`validation/island-edges-20261016/site-release.json`. The remaining larger-stage/full-run milestones are unchanged.

## Character art correction (2026-10-16)

The sitting pose retains its folded legs and breathing. The runtime also now advances past selected art deltas
that contain no changed rows, so later hair, cloth, markings and weapon layers still apply. The old public ROM
fails the independent pixel comparison; both replacement mapper images pass 2,660 sampled native uploads each.
The complete native gate, 128 seated-state/direction samples and live site/save read-back are recorded in
`validation/heir-art-20261016/`. This release preserves the ten-floor office scope.

Camera-driven terrain assembly/collapse remains on `codex/camera-tiles`. Both mapper builds now pass quiet travel/backtracking, diagonal reversal, actual combat, complete bodies,
120/120 frame timing, native BG/OAM reference comparisons and the full regression gate. Evidence is in
`validation/camera-20261016/`. Its camera changes are not yet in the public cartridge. A larger connected simulation
window, composed portal/circuit proof and the rest of the full-run ledger are still required.
