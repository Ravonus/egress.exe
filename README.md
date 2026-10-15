# EGRESS.EXE

A Game Boy Color game in the CRUCIBLE world: procedural Boulder Dash puzzle floors (Exodus style), one-weapon action
combat (Bastion style), a small roguelike run. The spec is [DESIGN.md](DESIGN.md).

## Build

Needs GBDK-2020 4.5.0 (`GBDK_HOME=$HOME/.cache/gbdk-4.5.0 sh ~/dev/crucible.exe/tools/install-gbdk.sh`; the path must
not contain ".exe"), a host C compiler, and a Python venv for the art, the music and the harness:

    python3 -m venv .venv && .venv/bin/pip install pyboy==2.7.0 pillow==12.3.0 numpy==2.5.3
    make            # build/egress.gbc: linked as MBC5 2 MiB, trimmed after its last bank, gated at 1,900,000 bytes
    make test       # host tests: gravity, the solver, determinism, every baked seed re-solved; the ROM gate's tests
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
first 128 seeds whose rooms are not trivial, need their trick (proved: the trick's obstacle counted as concrete seals
the goal), keep their herrings harmless, and solve within the tier's difficulty band; the cartridge only plays those
seeds. `make test` re-checks every baked seed; the harness checks that the cartridge's room is byte-identical to the
host's and drives the solver's plan, bench and all, through the real game to the door.

## A run

Ten floors. Floors 1-9 are generated rooms from the baked seeds, tiers 0,0,1,1,2,2,3,4,5; floor 10 is a built arena
with THE WATCHMAN (fan of orbs, summoned mites, a dive that leaves it stunned; drop the gallery's CRTs on it). After
floors 3, 6 and 9 there is a landing: one of the six factions talks (the reply leans the truth matrix and their
standing), then trades mods, a battery or coffee for tokens, priced by standing. The ending is a question built from
what the matrix leans toward. The battery save (two CRC-16 records in SRAM) keeps runs, wins, the best floor, the
unlocked mods, the things seen and a bounded trace of the matrix.

Controls: d-pad move (lean into a CRT to push it), A lash (three-hit combo; hold to charge, release to spin), B dodge
roll, START pause, tap SELECT for the bench (combine two pieces with A, START uses the chosen piece on the cell you
face), hold SELECT to reset the floor (costs half a heart).

## Layout

    src/            cartridge C (main, screens, play, grid, hud, player, actors, sprites, boss, run, sound) and
                    src/core (shared rules)
    src/gen/        generated: art (tools/bake_art.py), music (tools/compose.py), seeds (tools/bake_seeds.sh)
    host/           solver, egtool (show/solve/dump/tier), tests
    tools/          art bake, composer, seed bake, ROM finish + gate
    test/harness/   PyBoy harness
    docs/shots/     screenshots from the harness and the art contact sheet

MIT licensed (LICENSE).
