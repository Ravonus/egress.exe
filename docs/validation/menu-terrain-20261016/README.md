# Native menus, crafting, survival waves and terrain

The previous native heir draw call passed hardware Y at player centre minus seven. With the Game Boy's
16-pixel OBJ bias and the WEAR layer offset, this lifted the character eight pixels above the intended
12x12 movement footprint. The corrected call places the feet at centre plus six. World and camera
coordinates, tile dimensions and save layouts are unchanged.

Drywall now uses the same weapon-specific directional rectangle as combat, intersected with complete
tile bounds, instead of a single point twelve pixels in front. Cardinal, diagonal and charged attacks
retain their weapon reach and timing.

Camera construction looks four cells ahead across a wider strip. A bounded nearby scan settles
structures before contact. Each movement step checks all destination foot-box corners after ordinary
collision and preserves movement on an already built axis. Camera-owned hidden terrain is distinct
from actual pits, fragile failures and island edges. Portals, recovery and obstacle shoves also land on
built terrain. The terrain pool, OBJ scanline limits and per-frame commit budgets remain bounded.

The native lobby, controls, confirmation, pause and two-page heir picker share four reserved BG tiles
(one texture and three flipped frame primitives). Menus use plum and cream, matching the office palette.
Three 32x48 heir portraits are composed once from the canonical WRAM pose and cached in BG VRAM,
with body/cloth colour matching. Nine palette bitmask bytes alias the floor-only skin scratch;
no extra gameplay BSS or sprite slots are consumed, and the picker never reads active VRAM.
Changing selection updates borders, cursor and status without clearing the page or blacking palettes.
Detail selection retains native idle animation. Both legal title background-map exits are supported without reading either map;
A browser screenshot caught the fixed-map read in the rejected preview,
and the new regression fails on that rejected ROM while passing on both corrected variants. Sibling clothing silhouettes and hair styles differ;
only unchosen candidates receive the cosmetic update. Active heirs and ancestor genomes are preserved.

Focused tests observe every LCD frame during next/previous/detail/back, cached portrait bytes, title
and footer ROM-bank correctness, complete feet/OAM alignment, centered passages, pause/resume state,
all eight fastest blink directions, complete destination footprints, and twelve off-centre wall attacks.
The world replay observer now checks all four foot-box corners, rather than only the centre cell.

See `manifest.json` for exact source/ROM hashes and the full gate result. Native PyBoy and browser
verification are separate from physical Chromatic testing; no physical cartridge was written.

The item-card fixture now lets an already-started previous-card transfer finish before sampling unrelated
VRAM. The observed failure changed only DUST bytes while the fixture selected CLOUD mid-call. Its
150-frame, all-other-cards byte comparison and four-phase coverage remain intact.

Terrain timing retains the full movement guard. Duplicate corner probes are removed, nearby cosmetic
work gets at most one commit, and the rolling scan rechecks its deadline per probe. The original
12-frame seam-wait and 120-updates-per-120-displays gates are unchanged.


The bench now uses the full native window width for the recipe scene and a compact
six-item inventory strip with finite counts. Ingredients approach, make contact,
react and reveal the canonical recipe result, with distinct sound cues and the
same four baked material idle poses. No inventory unit is consumed during the
approach or reaction. Invalid recipes keep both units; a completed recipe consumes
two and returns one result. Its functional descriptions reflect implemented uses.

Survival rooms spawn enemies anywhere the player can fight, without charging a
standing zone. Each cleared wave grants a 90-frame breather. Finite reinforcements
increase by one per wave, plus stage-level scaling, capped at eight per wave;
only three enemies are simultaneously live. Three native art slots cover walkers,
ranged enemies, and the stage's unlocked swarm or armoured role. Later waves add
health and movement pressure. Victory requires clearing every reinforcement.
Existing stage IDs and save layouts are retained. Native fixtures cover levels
0, 5 and 10 using controller attacks, without enemy-health writes or forced kills.

The remaining portal hitch came from doing speculative view binding in the same
frame that a roll entered a portal. A player-side forecast reserves that frame
for actual transport, while arrival locks and unpowered pads keep normal prefetch.
The original 120/120 and maximum 12-frame seam-wait gates remain unchanged.


Combat performance uses bounded reinforcement setup, seeded single-pass steering,
byte-sized cached mob positions shared by body and overlay, and reused weapon
frame profiles. Hitstop begins before moving other actors. Optional terrain and
particle work defers when the display budget is spent; the full player body stays
cached until a new complete pose is ready. Page-aligned sprite/window uploads use
byte increments within their proven boundaries. The game waits on its monotonic
VBlank epoch, preventing a lost wait flag at the display boundary.

Wave timing observes each completed native update against VBlank epochs and also
rejects any frame that finishes after its epoch. Reading `frame_count` exactly at
LCD line zero can sample either side of ISR return, yielding a paired 0/2 sample
without a skipped update; those raw samples are preserved as diagnostics. All six
level/mapper cases require 120 completed updates over exactly 120 VBlanks and zero
late completions. Existing camera/world timing and presentation checks remain
separate release gates.
