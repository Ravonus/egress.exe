# EGRESS.EXE (working title)

A Game Boy Color game set in the CRUCIBLE world. It plays like Exodus: Journey to the Promised Land (NES): grid
puzzle rooms with pushable and falling blocks, keys and exits, in the Boulder Dash style. Its combat is Bastion-like:
one main weapon, readable enemies, dodge, and an action feel that's as good as the GBC allows. Runs are a small
roguelike. Everything is generated and procedural the way KEEL does CRUCIBLE's art.

## Hard limits
- **The ROM file must be at most 1.94 MB** (the cartridge's own limit, from the owner on 2026-10-14). The build
  gate is **1,900,000 bytes**. The header declares MBC5 2 MiB (code 0x06) and the image is **trimmed**: the
  unused tail banks are cut, so nothing may be placed past the gate. The header checksum is computed over the
  trimmed file, and the global checksum is fixed after trimming. The gate fails if any bank above the cut is
  referenced.
- **GBC only:** CGB double speed, GBDK-2020 4.5.0 (`~/.cache/gbdk-4.5.0`, installed by
  `~/dev/crucible.exe/tools/install-gbdk.sh`; the path must not contain ".exe").
- **Deterministic:** seed plus inputs, so runs replay and link play can run in lockstep later.

## World and tone (CRUCIBLE canon)
- Backrooms meets dreamscape. You wake with no explanation, and the truth of the place shifts as you play. It's never
  rolled; it's driven by what you do. Endings are questions.
- The same factions as CRUCIBLE: COMPILE, CHARGE, SKY, WATCH, ROOT, LOCK. They like you or hate you for what you
  do and say.
- Wacky, with an edge. Don't sanitise it; remove only hate symbols, hanging, sexual content and caricature. Mystic
  lines must never be cheesy.
- The story extends CRUCIBLE. You are looking for the way *out* of the bench room's building, floor by floor
  (the Exodus "journey" structure).

## Core loop
1. **Floor:** a procedural puzzle room on a 20×18 screen grid, or a few screens with scrolling. It has pushable
   blocks, falling objects, crumbling floor, switches, keys and a locked egress door. Every generated room must be
   **verified solvable** by a solver at generation time (generate, then solve, then accept; otherwise reseed). The
   solver can run offline to bake seed tables if the CGB is too slow.
2. **Enemies** roam the rooms. **One main weapon**, Bastion style: a primary attack, a charged/alternate use and a
   dodge roll. The weapon is upgraded through the run (mods slot into the single weapon) rather than swapped.
   Enemies have tells and patterns, and there are i-frames on dodge.
3. **Tokens** are collected in rooms and from puzzles (the collectible currency, like Exodus' manna and question
   pickups). They're spent at traders.
4. **Traders and talkers** appear between floors. The **chat system is a matrix**, like CRUCIBLE's truth matrix:
   what you said and did moves faction standing and shifts which lines and truths unlock. **Trading** swaps tokens
   and found objects for weapon mods and consumables; rates depend on faction standing.
5. **Roguelike:** a run is about 8–12 floors and a boss. Death ends the run. Meta-progress, kept small, is saved:
   unlocked mods enter the pool, the truth matrix keeps a bounded trace, and the book of things you've seen grows.

## Look (same pipeline ideas as KEEL/CRUCIBLE)
- KEEL-style dithered pixel art, never smooth. Objects come from procedural constructions with seeds and are rendered
  to tiles at build time, with seeded colour variants (humans get diverse colours).
- Advanced GBC colour, like CRUCIBLE: per-tile CGB palettes, a sprite material overlay layer for extra colours,
  palette animation (light, water, glitch) and mid-frame palette changes where they're affordable.
- A KEEL boot screen first, then the title.
- The art is reviewed visually for content (see CRUCIBLE's asset review rules), and every shape must match its name.

## Sound
- Warm, ambient, original chiptune, like CRUCIBLE's current trailer-style music. No harsh loops. Music and SFX
  volumes, with fades.

## Reuse (read, don't copy blindly)
- `~/dev/crucible.exe`: the public CRUCIBLE source (the build tooling, PyBoy test harnesses, the music driver, the
  face/visitor generator, the truth matrix, the HOST CLOCK, the save CRC patterns). It's MIT-licensed, the same owner.
- `~/dev/rictus/alchemy`: the KEEL generation and bake pipeline for CRUCIBLE (tools/, cartridge bake, palettes,
  material overlay).

## Milestones
- **M0:** the repo, the build, the 1 MiB budget gate and a PyBoy harness that boots, plays seeded inputs and
  screenshots.
- **M1:** a playable vertical slice: one generated, solver-verified puzzle floor; a player with the weapon, a dodge
  and two enemy types; tokens; an exit.
- **M2:** the run structure (floors, a boss), a trader plus the chat matrix, weapon mods, saves.
- **M3:** the art pipeline at full colour, music, the story layer, polish.
