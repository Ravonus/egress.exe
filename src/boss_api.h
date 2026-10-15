#ifndef BOSS_API_H
#define BOSS_API_H
/* B2 bosses (docs/redesign.md 7.5, 3.6.6, 12b): the mid-boss and the biome boss slots. The contract other branches
 * build on; everything here is banked (src/boss_*.c), nothing is in ROM0.
 *
 * A boss floor is a built arena, not a generated room. b2-stages puts a boss in a floor slot like this:
 *
 *     boss_floor = BOSS_MID;           (or BOSS_BIOME; 0 is a normal floor)  -- play.h's boss_floor
 *     floor_build():  if (boss_floor) boss_setup(boss_floor, BIOME_OFFICE); else eg_generate(...)
 *     floor_show():   ... draw the room ...; if (boss_floor) boss_show();
 *
 * and play.c's floor loop does the rest through the hooks below (already wired on b2-boss). The floor ends the usual
 * way: pl_state == PS_EXIT after the boss beat (the floor collapses into the next biome, or the mid-boss's door), or
 * pl_hp == 0. boss_result says which, and heart_shards counts what the bosses dropped.
 *
 * The office has two bosses (B2):
 *   BOSS_MID    LAST FAX: a fax machine on rollers. 2 phases. It rams (dodge), jams (dodge), spits pages (parry).
 *   BOSS_BIOME  THE TENANT: it lives in the north wall (its body is background tiles: no sprites). 3 phases, each
 *               gated by the room: the lights go, then the floor goes. Rubber-stamp hands slam and sweep (dodge),
 *               its screen fires volleys (parry) and beams (dodge), it shakes CRTs off the shelves (move).
 * Every attack has a tell of 24 frames or more: a pose, a palette flash and its own sound. Nothing hits off screen
 * (the arena is one screen and the camera holds still). The harness's no-hit bot beats both (test/harness/boss.py). */
#include "play.h"

enum { BOSS_NONE = 0, BOSS_MID = 1, BOSS_BIOME = 2 };

/* --- the slot API (b2-stages) --- */
void boss_setup(uint8_t slot, uint8_t biome) BANKED; /* floor_build: the arena into `room`, the boss into en[0] */
void boss_show(void) BANKED;                          /* floor_show, display off, after the cells are drawn */
extern uint8_t boss_result;   /* 0 fighting, 1 beaten (the floor is ending through PS_EXIT), 2 the heir died */
extern uint8_t heart_shards;  /* 3.6.6: each mid-boss and boss drops one; 4 make a heart (pl_hp_max += 2) */
extern uint8_t boss_cause[3]; /* 12b.4 VERB, KILLER, TWIST when a boss killed the heir (the card reads it) */
extern uint8_t dbg_boss;      /* harness: nonzero = the dbg floor is this boss slot */
extern uint8_t bs_kind;       /* the boss in the arena now (BOSS_*), 0 off a boss floor */
/* where the office's bosses stand in a run until b2-stages places them (play.c floor_build) */
#define RUN_MID_FLOOR 5u

/* the arena's fixed camera (the whole arena is one screen) */
#define BOSS_CAM_X 48u
#define BOSS_CAM_Y 48u

/* --- hooks in the B1 modules (one line each) --- */
void boss_vblank(void) BANKED;            /* play.c, first thing after frame_wait: palette tells, the face's tiles */
void boss_death_record(void) BANKED;       /* player_hurt: record the cause on the exact lethal hit */
void boss_death_caption(char *out) BANKED;
void boss_update_all(void) BANKED;        /* boss.c boss_update for SPAWN_BOSS: the AI, the beats, the flows */
void boss_hit(uint8_t dmg, uint8_t fromx, uint8_t fromy) BANKED; /* actors.c enemy_hit, kind SPAWN_BOSS */
void boss_crush(void) BANKED;             /* actors.c actors_crush: falling CRTs on boss parts */
/* the boss's own sprite classes for b2-mobs' draw_sprites (src/oam.h): call from OP_MARK and OP_BOSS */
void boss_draw_marks(void) BANKED;
void boss_draw_body(void) BANKED;

/* --- living obstacles (7.7; cartridge adapter in obstacle.c) --- */
enum { BOC_SUMMON = 0, BOC_REVERSE, BOC_WAKE, BOC_FETCH, BOC_LIGHTS };
uint8_t boss_obstacle_cmd(uint8_t cmd, uint8_t cell, uint8_t rule) BANKED; /* 1 if something answered */
void boss_obstacles_clear(void) BANKED; /* the boss fell: summoned obstacles glitch out */

#endif
