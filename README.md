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

`src/core/eg_room.c` is shared verbatim by the cartridge and the host. It generates a 16x14 room from (seed, tier)
and runs the CRT gravity step (fall, roll, held up by the player, hit the player). The host solver
(`host/eg_solve.c`) searches settled boards, one board-changing act per step (dig, collect, push, break drywall, leave
a cell that holds a CRT up), and every plan it returns is replayed through the same gravity code. `tools/bake_seeds.sh`
keeps, per tier, the first 128 seeds whose rooms are not trivial (a keycard or the door reachable without moving
anything) and solve within the tier's bounds; the cartridge only plays those seeds. The harness checks that the
cartridge's room is byte-identical to the host's and drives the solver's plan through the real game to the door.

## A run

Ten floors. Floors 1-9 are generated rooms from the baked seeds, tiers 0,0,1,1,2,2,3,4,5; floor 10 is a built arena
with THE WATCHMAN (fan of orbs, summoned mites, a dive that leaves it stunned; drop the gallery's CRTs on it). After
floors 3, 6 and 9 there is a landing: one of the six factions talks (the reply leans the truth matrix and their
standing), then trades mods, a battery or coffee for tokens, priced by standing. The ending is a question built from
what the matrix leans toward. The battery save (two CRC-16 records in SRAM) keeps runs, wins, the best floor, the
unlocked mods, the things seen and a bounded trace of the matrix.

Controls: d-pad move (lean into a CRT to push it), A lash (three-hit combo; hold to charge, release to spin), B dodge
roll, START pause, hold SELECT to reset the floor (costs half a heart).

## Layout

    src/            cartridge C (main, screens, play, grid, hud, player, actors, sprites, boss, run, sound) and
                    src/core (shared rules)
    src/gen/        generated: art (tools/bake_art.py), music (tools/compose.py), seeds (tools/bake_seeds.sh)
    host/           solver, egtool (show/solve/dump/tier), tests
    tools/          art bake, composer, seed bake, ROM finish + gate
    test/harness/   PyBoy harness
    docs/shots/     screenshots from the harness and the art contact sheet

MIT licensed (LICENSE).
