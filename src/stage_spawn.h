#ifndef STAGE_SPAWN_H
#define STAGE_SPAWN_H
/* Stage roles deliberately share the FAM_* numeric IDs in mob.h. */
#include "play.h"

enum {
  SS_CHASER = 0, /* mob.h FAM_INTERN (walker, lunge)        today: SPAWN_MITE */
  SS_CRUSH,      /* mob.h FAM_CABINET (only a falling CRT)   today: SPAWN_MITE, the stage enforces KC_CRUSH */
  SS_ORB,        /* mob.h FAM_LAMP (only its own orb back)    today: SPAWN_EYE, the stage enforces KC_ORB */
  SS_SHOOTER,    /* mob.h FAM_FAX (hover, projectile)         today: SPAWN_EYE */
  SS_BACK = 5,   /* mob.h FAM_MEMO (armoured front)           today: SPAWN_MITE, the stage enforces KC_BACK */
};
#define SS_NATIVE_CONDS 1u

/* one mob now at a cell centre (mob_spawn): the en[] index, or 255 (en[] full, or not open floor). Spacing is the
 * caller's (ss_spawn_ok). `elite` 0/1 (today: more hit points). */
uint8_t ss_spawn(uint8_t fam, uint8_t cell, uint8_t elite, uint16_t seed) BANKED;
/* mob_spawn_ok: open floor, >= 3 cells from the player, >= 2 from any live mob, <= 2 live mobs on the row */
uint8_t ss_spawn_ok(uint8_t cell) BANKED;
/* mobs_alive: live mobs (the wave-clear test) */
uint8_t ss_alive(void) BANKED;
/* mobs_preload: upload the looks for families a stage spawns later, display off */
void ss_preload(uint8_t fams) BANKED;

#endif
