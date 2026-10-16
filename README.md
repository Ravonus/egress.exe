# EGRESS.EXE

A Game Boy Color game in the CRUCIBLE world: procedural physical and elemental puzzles, two-button action combat,
and inherited heirs. The current cartridge is a ten-floor office development slice. The longer run in
[DESIGN.md](DESIGN.md) is not yet the native game.

The recovered B2 work and current verification are recorded in [the continuation notes](docs/recovery-20261015.md).
The subsequent [gameplay rebuild](docs/gameplay-rebuild-20261015.md) records the implementation contract,
completed native systems, controls, evidence and remaining work.
The [completion ledger](docs/SPRINTS.md) tracks the continuing work beyond that published checkpoint.

## Build

Needs GBDK-2020 4.5.0 (`GBDK_HOME=$HOME/.cache/gbdk-4.5.0 sh ~/dev/crucible.exe/tools/install-gbdk.sh`; the path must
not contain ".exe"), a host C compiler, and a Python venv for the art, the music and the harness:

    python3 -m venv .venv && .venv/bin/pip install pyboy==2.7.0 pillow==12.3.0 numpy==2.5.3
    make            # build/egress.gbc: linked as MBC5 2 MiB, trimmed after its last bank, gated at 1,900,000 bytes
    make test       # host + both ROMs: rules, solver, family/save, inputs, native play, art and ROM gates
    make test-b2    # cartridge stages, both no-hit bosses, sprite and combat frame budgets
    make shots      # PyBoy harness: boot, a solved floor, combat, death + the save, a landing, the boss, the ending
    make seeds      # re-bake the accepted-seed table (slow: minutes of solving)

The ROM gate (`tools/finish_rom.py`) fails the build if anything is linked into a bank that would end past
1,900,000 bytes, or if any non-padding byte lies past the cut; it writes the header and global checksums over the
trimmed file.

## How rooms are made

`src/core` is shared verbatim by the cartridge and the host. `eg_gen.c` builds a 16x14 office room backwards from its
door: it picks a trick (BURN paper and planks with something HOT, COLD PATH: freeze a flood to walk over it, SHORT
CIRCUIT: shock a flood that touches a dead socket and the shutter under the door lifts, DEAD WEIGHT: lay a brick on a
plate in the north wall), seals the door behind it, then places the pieces (FIRE, WATER, AIR, EARTH, METAL) that the
bench (`eg_piece.c`: 11 recipes, the 8x8 trait reaction table) turns into the answer, each behind a lock of its own,
plus a red herring or two. Floors are mostly open carpet: foam comes in a few deliberate patches (under 25% of every
baked room). `eg_room.c` runs the CRT gravity step. The host solver (`host/eg_solve.c`) searches settled boards, one
board- or pocket-changing act per step (dig, collect, pick up, push, break drywall, combine on the bench, use a piece
on a cell), and every plan it returns is replayed through the same rules. `tools/bake_seeds.sh` keeps, per tier, the
128 seeds whose rooms are not trivial, need their trick (proved: the trick's obstacle counted as concrete seals
the goal), keep their herrings harmless, and solve within the tier's difficulty band; the cartridge only plays those
seeds. `make test` re-checks every baked seed; the harness checks that the cartridge's room is byte-identical to the
host's and drives the solver's plan, bench and all, through the real game to the door. The bounded bake includes
five isolated STEER/MIRROR lessons that are revalidated with the full tick-state solver, so a shorter search cannot
silently remove either mechanic from the curriculum.

Fragile floor (`eg_room.h` eg_frag: worn carpet 3 steps, ceiling tile 2, glass 1, thin ice 2, glitched glass on a
beat) wears by stepping off it; the last step leaves the void. Two more tricks use it: one-way glass (the keycard lies
behind you) and glitched glass (cross on the beat). Breaking the wrong tile can make a room unwinnable: the cartridge
notices the common cases (a flood fill after every break or push), says ROOM FORGETS, and holding SELECT puts the
room back for a token, never a heart. Which room a floor gets is the run planner's (`src/core/eg_plan.c`): mechanics
are introduced alone, developed, then combined, on a tension-release-spike wave that rises with depth and with the
runs played (`host/egplan sim`, `tools/plot_curve.py`).

## A run

Ten floors in the office slice. Floor 1 teaches pushing crates onto plates, floor 2 requires defeating waves,
floor 3 drops a CRT onto a plate, and floor 7 offers a physical CRT crossing or a finite crafted mud crossing.
Floors 6 and 8 deal chase/escort and hunt/holdout fights. Elemental lessons on floors 4 and 9 draw from the 768
verified seeds, including living obstacles across later generations; the first family starts with circuits and
cold paths, with fire introduced later. LAST FAX is the mid-boss on floor 5; THE TENANT is the biome boss on floor 10. After
floors 3, 6 and 9 there is a landing: one of the six factions talks (the reply leans the truth matrix and their
standing), then trades mods, a battery or coffee for tokens, priced by standing. The ending is a question built from
what the matrix leans toward. The legacy battery record keeps runs, wins, the best floor, unlocked mods and the
truth matrix. A separate CRC-checked two-slot family journal preserves the selected heir, full lesson history,
three related successors and the last eight actual ancestors, without moving the legacy save or either clock.
Heirs inherit appearance, stats, weapon and dodge alleles; blade, hammer, whip, gauntlet and launcher have different
timing, reach, force and secondary attacks. THE TREE on a landing shows the real family history.

The satchel carries 24 finite units between floors. SELECT opens the two-slot, eleven-recipe bench with inventory
icons and quantities, animated ingredients/result, the material faced and START-to-use. Invalid recipes and targets keep the items. A valid use
consumes one unit; a valid recipe consumes two and creates one. Hold RIGHT in the bench to discard one unit, then
release before discarding another. A room reset restores its entry inventory, preventing pickup duplication.

The lobby offers continue/new run, controls, tutorial and voice settings. Continue regenerates the saved floor from
its entry state while retaining damage, or resumes a landing's conversation/trade state. Real falls cost health and
return the heir to firm ground. A forward roll can cross a short hole. Plates respond to the player's weight, and
an irreversibly stranded required crate triggers the reset cue while alternative tool routes remain valid.

Eight original fal speech prototypes react to real hits, falls, crossings, broken walls and other recorded events.
They are baked into both ROMs as 8,192 Hz four-bit PCM (96,432 bytes total); runtime playback needs no service or API
key. `tools/narrator/bake_assets.py --check` reproduces their banks from the retained source WAVs. The broader
event grammar and the remaining whole-game work are described in the ledger.

Controls: d-pad move (lean into a CRT or crate to push it), A lash (three-hit combo; hold to charge and release),
B tap for the secondary attack; hold B to charge and release to execute it. B then A rushes; A then B counters. Both attacks aim in eight directions. Double-tap a direction to evade (sideways sidesteps, backward backsteps); hold the direction to run. Each action spends stamina; resting restores it. START shows controls and pauses. Tap SELECT for the bench
(combine two pieces with A, START uses the chosen piece on the cell you face); hold SELECT to reset for a token.

## Layout

    src/            cartridge C (main, screens, play, grid, hud, player, actors, sprites, boss, run, sound) and
                    src/core (shared rules)
    src/gen/        generated: art (tools/bake_art.py), music (tools/compose.py), seeds (tools/bake_seeds.sh)
    host/           solver, egtool (show/solve/dump/tier), tests
    tools/          art bake, composer, seed bake, ROM finish + gate
    test/harness/   PyBoy harness
    docs/shots/     screenshots from the harness and the art contact sheet

MIT licensed (LICENSE).
