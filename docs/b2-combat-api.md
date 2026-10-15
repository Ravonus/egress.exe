# B2 combat, spawn and sprite API (b2-mobs)

Published early so b2-boss, b2-stages and b2-obstacles can build against it. The headers are the contract:
`src/mob.h` (mobs, spawning, combat, reactions, the look pipeline) and `src/oam.h` (the per-line sprite budget,
priority and flicker). This page is the short version. Names may gain members; existing ones keep their meaning.

## Who owns what

- b2-mobs owns `src/actors.c`, `src/sprites.c`, `src/mob*.c`, `src/combat.c`, `src/fx.c`, `src/oam.h`, `src/mob.h`,
  `tools/bake_mobs.py`, `src/gen/mob_art.c`.
- Small, delimited hooks in `src/player.c` (it sets `hit_src` / `hit_tags` before each strike) and nothing else
  shared.

## Mobs (b2-stages, b2-boss)

- A mob is `en[k]` (play.h) with `kind == SPAWN_MOB`, plus `mob[k]` (axes, family, look slot, statuses, elite
  affixes). The B1 kinds (mite, eye, boss) are unchanged; a boss can keep summoning mites.
- `mobs_floor()` runs from `actors_spawn()` and turns `room.spawn[]` into the floor's roster.
- Mid-floor spawns: `mob_spawn(fam, cell, elite, seed)`, with `mob_spawn_ok(cell)` for spacing (open floor, at least
  3 cells from the player, at least 2 from a live mob, at most 2 live mobs on that cell row). Looks for families a
  stage will spawn later go in with `mobs_preload(mask)` while the display is off (at most 3 looks a floor).
- `mobs_alive()` is the wave-clear test.

## Combat (b2-boss)

- Damage still goes through `enemy_hit(k, dmg, fromx, fromy)`. For a matrix mob it routes to `mob_hit()`, which
  applies:
  - armour and shields;
  - the kill condition;
  - the reaction table (`combat_react`, the one `eg_rx_table` of 5.2);
  - knockback (a third for heavies, none for turrets);
  - hitstop (`freeze`, 2–6 frames by damage).

  The result is in `hit_result` (`HR_*`).
- `hit_src` says what hit:
  - one of the six moves: `HS_HIT1..3`, `HS_CHARGED`/`HS_SPIN`, `HS_LUNGE`, `HS_PARRY`, `HS_DODGE`, `HS_SPRINT`
    (rolling or running through a small swarm mob tramples it);
  - a returned orb (`HS_ORB`, with `hit_orb_from` = the mob that fired it);
  - a crush (`HS_CRUSH`), the void (`HS_VOID`), `HS_FUSE` or a reaction.
- `hit_tags` are the weapon's tag bits (5.3 mods).
- Set both before calling `enemy_hit` from new code. A boss phase can use the same conditions (an armoured front, its
  own reflected orb, crush only) by reading `hit_src` in its own damage path.
- Particles: `fx_burst(x, y, FX_*, n)`. The pool is 8, recycled oldest first, and drawn last.

## Sprites: the 10-per-line rule (everyone who draws)

- Everything goes through `oam_group(sx, sy, n, tile0, tstep, prop)`. It places n 8x16 OBJs side by side, all or
  nothing, only if no 4-line band would exceed 10.
- Drawing order is priority:
  1. heir;
  2. weapon;
  3. elite and boss markers;
  4. boss body;
  5. shots;
  6. mobs, from a rotating start (`oam_spin`);
  7. overlays;
  8. particles.

  A boss arena that wants more than 10 on a line gets flicker for its lowest class instead of hardware drop-out.
- `oam_band_max()` and `oam_refused` are for the harness. The harness also checks the real shadow OAM every frame.

## Looks (b2-obstacles)

- `tools/bake_mobs.py` renders looks with `tools/mock/mobgen.py` (the 7.6 generator) and bakes them, D3 style
  (dithered ramp, 1 px inner outline), into 8x16 tiles:
  - frames idle 0, idle 1, tell and dissolve for the body;
  - one overlay for the accessories and tag tells.
- A living obstacle can be another family row in that tool's `LOOKS` table, loaded with `mob_look_load(slot, look,
  group)` and drawn with `mob_look_draw`.
- VRAM:
  - sprite tiles 16..87: 3 look slots of 24, freed when the heir moved to tiles 0..15;
  - 88..111: particle and marker shapes (crown, dot, star, shard, drop, ember, bolt, leaf, rune, clang, bump, ring);
  - 112..133 stay B1's mite, eye and orb for boss.c;
  - 210..255 stay the look's.
- OBJ palettes on a floor:

  | Palette | Use |
  |---|---|
  | 0, 1 | heir |
  | 2, 3 | the floor's two mob body ramps (element-tinted) |
  | 4 | weapon |
  | 5 | overlay and FX: ink, then each group's cue colour |
  | 6 | hit flash |
  | 7 | tell and elite |
