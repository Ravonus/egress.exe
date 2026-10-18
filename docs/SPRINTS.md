# Native game completion ledger

This tracks the complete build order in `redesign.md` section 18 and the owner's later gameplay corrections.
The development cartridge includes native feedback, island edges, inherited-art corrections,
camera terrain, composed circuits/portals and quiet generated worlds, plus framed menus/heir selection,
visible bench merging and escalating finite survival waves.
It does not complete this ledger.
An integrated prototype, a passing host simulation, a native build and a played feature are separate evidence.

| Milestone | Current state | Required before completion |
|---|---|---|
| Core, movement and rendering | Office checkpoint verified | Keep current two-action-button controls, full bodies and frame/OAM budgets through the full game |
| Puzzles and finite tools | Partial | Composed dependency motifs, varied topology and states, resource-feasible alternatives, wrong-spend branches and native replay |
| Authored stage codec and generated geography | Quiet native worlds verified on both mappers; campaign integration partial | Reduce measured corner preparation waits; actor streaming, campaign curriculum, environment/reaction coverage and continuation |
| Shared action/object/mob matrix | Partial | Complete the authored interactions and readable activation, reaction, spent and destruction states; exercise both directions |
| Mobs, stages, biomes and full run | Partial | Native full-run layout and seed bank, all biomes, stage/obstacle combinations and their kill/exit guarantees |
| Heirs, lineage, saves, tree and stats | Partial | Current SRAM journals, floor/landing continue, shortcut/line progress, full tree and stats pages with event accounting |
| Boss roster | Two office fights verified | Remaining biome bosses, final stretch and no-hit feasibility/frame budgets |
| Music | Existing score player | Native adaptive composition, transitions and in-key effects across the complete run, with captured/listened audio |
| Title and first-person stages | Title verified; renderer prototype | Playable generated first-person stages with the same tools/combat and measured timing |
| Balance | Host model and office checks | Full native run coverage, survival/resource/build variety gates and human play feedback |
| After the egress | Designed | Native descent, cycle/shortcut consequences, deeper truths and persistent records |

## Current work

Owner correction, 2026-10-18: terrain now assembles only at spawn. Camera travel, backtracking, exits and death keep ordinary floor resident. Real destructible terrain still animates. Material collision owns movement and lifting; visual-presence flags cannot block open floor. The complete native regression and final website/browser checks pass; evidence is in `validation/spawn-only-20261018/`. This supersedes the camera build/drop presentation described in earlier dated checkpoints.

The current release requires a fresh save, as requested by the owner on 2026-10-17. Earlier family, run,
meta-progress and tutorial journals are rejected; the FA1/FA2 family migration and saved-floor migration paths
are deleted. The browser reads only the current cartridge's battery namespace. Current floor-entry/landing
journals preserve damage, inventory, the chosen heir and narration history; interrupted end transactions finish
once. This is a ten-floor office branch, still short of the full run. The dated checkpoint notes below retain the
evidence for earlier releases; their migration policies no longer describe the current cartridge.

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
This avoids the 128-byte sky record at A240 and the family/tree journals. The current settings epoch rejects
earlier tutorial records. Core/native checks cover both mappers, prior-save rejection and torn writes.

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

## Named circuits and composed portal motif (2026-10-16)

The continuation branch adds independent AND/OR/latching circuits, socket and player/object sources, conditional
bidirectional portals, crate/wind/CRT transport, and safety-held gates that do not falsely power a portal. The
metadata fits the room's remaining twelve bytes. A 512-state standalone cartridge trace compares complete rooms,
dirty cells and results to the portable rules. Constructor scratch uses a bitset, reclaiming 196 bytes without
changing the fixed 4,864-case legacy room/RNG fingerprint.

The office's seventh floor can now deal a composed two-island motif. One load powers transport; a separate gate
accepts a crate or a crafted weight; a final paper cut still needs heat. The host checks 64 seed variants with
independent physical/tool answers, required transport and wrong-spend branches. Both mapper builds play physical
and crafted answers through normal input, cross back and forth without losing objects/inventory, recognize a
real bad craft, and restore the floor through the existing reset. Item idle uploads now address their actual
metatile rather than overwriting the cracked ice and preceding object. Native hooks check all four uploaded poses.

This still fits one local simulation grid. Larger streamed stages and the remaining full-run milestones are
required. The complete native/host regression run passed; exact artifact evidence is in
`validation/linked-puzzles-20261016/`. The public cartridge remains on the previous tested release.

## Generative world correction (2026-10-16)

The owner additionally requests authored stage goals/conditions with procedurally generated map dimensions,
shapes, items and realizations, XS through XXL scales, horizontal shapes, fuller foliage, biome-shared variants,
and digital rivers/waterfalls. `STAGE-CODEC.md` defines the editable source and separates generated-world proof
from playable integration. The shared host/native constructor now covers those size and environment records;
the quiet streamed game and native environment renderer now pass ordinary-input acceptance. Do not count this
subset as completion of the sprint ledger.


## Persistent generated worlds (2026-10-16)

The cartridge now recycles a 16 × 14 active window across XS–XXL seeded worlds, up to 128 × 54 cells. Materials,
artwork, protection, cracks, finite pickups, moved crates, circuits and portals survive both-axis travel and
backtracking. The native environment art shares twelve metatiles and a bounded animation-card pool.

Both mapper images pass 42 ordinary-input scenarios, strict 120/120 frame timing, complete-body and independent
pixel uploads, BG/attribute reference checks, OAM limits and stack canaries. A separate real-input loss/reset
case passes on both. The complete `make -j1 test` regression passed on the exact frozen source and images.
Evidence and hashes are in `validation/world-stream-20261016/`. The public cartridge has not changed.

The input observer still measures up to 34 frames of terrain-preparation waiting at a few large-map corners.
That delay is separate from frame rate and remains optimization work. These first integrated worlds are quiet;
actor streaming, campaign-wide goals/biomes and continuation, the full run and remaining milestones stay open.

The subsequent card-span timing pass retains the same pool and world buffers, reduces the largest observed
window-margin wait from 34 to eight frames, and adds an independent native presentation-state oracle. Both
mapper images pass all 42 world scenarios, both resets and the complete regression again; see
`validation/world-reuse-tuning-20261016/`. The quiet-world scope and remaining full-game milestones are unchanged.


The camera/circuit/world continuation is merged through PR #1 and published from `70a9ecb`. Both live downloads,
metadata/previews, browser-emulator ROM identity, phone layouts and real older-save carryover are verified in
`validation/world-reuse-tuning-20261016/site-release.json`. All twenty remaining local branch heads are included
in main; only the primary game worktree remains, and the temporary offloaded test checkout was removed. This
release retains the ten-floor office campaign. The full native run and the other open milestones above remain
required.


## Native menu, crafting and wave correction - 2026-10-16

The current candidate replaces the lobby and heir navigation, aligns feet and
weapon/tile intersections, and prepares/guards the complete movement footprint.
The bench has a full-width recipe scene, real animated merging, traits, functional
hints and finite inventory icons. Survival encounters now require defeating finite,
escalating waves, with replenishment and later enemy roles instead of standing in
a ring. The release evidence is in `validation/menu-terrain-20261016`.
This checkpoint does not mark the full campaign or the remaining milestones complete.

## Native menus, crafting and survival release

The framed menus and distinct inherited portrait cards, corrected feet/tile boundaries, ahead-of-motion terrain preparation, visible finite-inventory bench merge and escalating enemy survival waves are merged and published. Both mapper images passed the complete native regression, real-input wave combat and world frame gates. The live website fetched those exact ROM bytes, preserved a legacy family/genome and crafted using real pickups. See `validation/menu-terrain-20261016/site-release.json`. This release does not complete the campaign ledger above.


## Terrain drops, fresh saves and offscreen waves - 2026-10-17

SELECT now activates a stable equipped material, with finite quantities, eight-way targeting, material-specific stamina costs, hand animation, native particles and sound. START then A opens an equip-only bag. Walk beside a seeded physical bench and press A after clearing mobs and pending waves to combine; SELECT equips, and new recipes equip automatically. Hold SELECT resets without casting. The slot is part of the floor-entry continue checkpoint. World prefetch prioritizes incoming terrain, starts vertical strips earlier, retires fully offscreen rows earlier, and admits expensive seed queries within a bounded early window. Bench hints yield during busy combat and camera transfers; apparatus hints and cosmetic glitch scans share the streamed-frame budget. Stage and boss completion avoid composing an immediately overwritten door message. The canonical world query uses the native speed compiler profile; generated materials and art remain byte-identical to the independent host oracle.

The current native checkpoint removes obsolete floor constructors and earlier-save migration paths. Fresh game progress is required. Floor cards use canonical artwork and consistent downward arrival/departure animation; quiet reversals finish their queues without extending moving/combat upload limits. Live wave and hunt arrivals remain beyond the current camera. The playable tutorial course and distinct eight-way hammer effects are included.

Both mapper images pass the full native regression and prior-save rejection checks. Browser acceptance verifies downloads, native portraits, fresh generation-one play, real pickups, finite animated crafting and changing sprite buffers. Evidence: `validation/terrain-course-hammer-20261017/`. This remains the ten-floor development cartridge; the full campaign and the other open milestones above remain required.

## Finite discovery crafting and individual critters - 2026-10-18

The active expansion replaces the prepared bench subset with a 444-identity discovery codec: 384 formed materials, 24 fuse bombs, 24 rechargeable tools and 12 critters. Its 5,283 symmetric pairs have finite routes from five base reagents. MIX, BAG and LOG are separate native pages, and the actual ingredient contours deform into the actual result. Collection knowledge does not create carried materials. Generated loot supplies base reagents. The accepted 768-room bank has been regenerated and solver checked.

Critters have directional walking/flying/hopping/crawling, eight animation states, stable targeted colour variants, individual health and cooldowns, six combat roles, 24 named automatic/command abilities and SELECT commands. Ordinary death is permanent. Vine Frog's Second Spring is the explicit exception: once per individual at half health, with its spent charge retained through swaps, resets and matching save checkpoints. Medicine prioritizes the heir and then a living companion. Bombs and tools use the shared conditional combat/terrain rules and existing bounded projectile/OAM pools.

Focused functional acceptance has passed on both mappers, including 76 critter and 21 crafted-effect checks each, actual bench collection, combat, old-save rejection and durable current records. All 43 focused phases passed on both mappers, including strict wave deadlines and extended-world travel without generation waits. Packed visibility and exact sprite-order scans reduce frame costs; the existing world store is prepared before play. The current-source complete native regression and website/browser gates now pass; publication and temporary-host cleanup are recorded with the release receipt. Evidence and remaining limitations are tracked in `validation/discovery-catalogue-20261018/` and `DISCOVERY-CODEC.md`. This does not complete the full campaign, boss/biome roster, tree/stats, adaptive score, first-person stages or cycles.

The owner visual correction uses canonical substance silhouettes, quieter connected carpet dithering and the approved trailing drop. Incoming floor is prepared offscreen one full tile earlier; the visible rising-placement experiment was not published. Initial loading retains its short rise from empty space. Native controller capture separately proves offscreen restorations and visible departures. The complete gate exposed item-idle starvation and a paused OAM publication defect. Those routes now pass with sparse quiet-room uploads and complete OAM publication. The reset driver waits for native assembly and the Fax driver holds its firing aim through release/startup; their original victory, no-hit, hit-count and timing requirements remain. The revised exact pair now passes all 43 focused phases, the complete native regression and website/browser acceptance.

Verified candidate source `ad859cab67bf1807169778200a87950e8bc3f5d8` and both ROM identities are recorded in `validation/discovery-catalogue-20261018/`. Incoming floor is prepared offscreen one full tile earlier; outgoing cards keep their downward flight. Current browser/native captures and site texture provenance match the downloads. This verification does not close the remaining campaign milestones.
