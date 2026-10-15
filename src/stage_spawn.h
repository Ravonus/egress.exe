#ifndef STAGE_SPAWN_H
#define STAGE_SPAWN_H
/* The stages' thin adapter onto the mobs (b2-stages). Written against b2-mobs' published API (src/mob.h at b2-mobs
 * 72d7753: mob_spawn, mob_spawn_ok, mobs_preload, mobs_alive; docs/b2-combat-api.md), implemented in
 * src/stage_spawn.c on today's actors (the B1 mite and eye) until that branch merges. At the merge, stage_spawn.c's
 * bodies become one-line calls into mob.h (the mapping is written next to each), and SS_NATIVE_CONDS turns 1: the hunt
 * target's kill conditions are then the mob's own defences (MD_CRUSH, MD_REFLECT, MD_ARMOR) and src/stage.c stops
 * enforcing them itself.
 *
 * Families: the stage asks for a role; the adapter picks the body. */
#include "play.h"

enum {
  SS_CHASER = 0, /* mob.h FAM_INTERN (walker, lunge)        today: SPAWN_MITE */
  SS_CRUSH,      /* mob.h FAM_CABINET (only a falling CRT)   today: SPAWN_MITE, the stage enforces KC_CRUSH */
  SS_ORB,        /* mob.h FAM_LAMP (only its own orb back)    today: SPAWN_EYE, the stage enforces KC_ORB */
  SS_SHOOTER,    /* mob.h FAM_FAX (hover, projectile)         today: SPAWN_EYE */
  SS_BACK = 5,   /* mob.h FAM_MEMO (armoured front)           today: SPAWN_MITE, the stage enforces KC_BACK */
};
#define SS_NATIVE_CONDS 0u

/* one mob now at a cell centre (mob_spawn): the en[] index, or 255 (en[] full, or not open floor). Spacing is the
 * caller's (ss_spawn_ok). `elite` 0/1 (today: more hit points). */
uint8_t ss_spawn(uint8_t fam, uint8_t cell, uint8_t elite, uint16_t seed) BANKED;
/* mob_spawn_ok: open floor, >= 3 cells from the player, >= 2 from any live mob, <= 2 live mobs on the row */
uint8_t ss_spawn_ok(uint8_t cell) BANKED;
/* mobs_alive: live mobs (the wave-clear test) */
uint8_t ss_alive(void) BANKED;
/* mobs_preload: looks for families a stage spawns later, display off (today: nothing to load) */
void ss_preload(uint8_t fams) BANKED;

/* b2-boss's arena entry (stubbed in src/stage_boss_stub.c until that branch merges; delete the stub then). Called
 * from the mid-boss and boss slots while the floor is built: 1 if the boss branch built the arena and owns the
 * floor, 0 to play the stand-in (the mid-boss: a hunt with an elite target; the boss: B1's Watchman). */
uint8_t boss_arena_enter(uint8_t slot, uint8_t level) BANKED;

#endif
