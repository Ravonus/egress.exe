#ifndef EG_PLAN_H
#define EG_PLAN_H
/* The run planner: a procedural but designed curriculum over the baked rooms (docs/redesign.md 3.5).
 *
 * Every baked room carries its mechanics (MK_* bits) and its difficulty vector from the host solver (tools/
 * bake_seeds.sh). For each floor the planner picks a stage and a target, then the closest baked room:
 *   INTRODUCE  a mechanic the player has never met, alone, in a gentle room (the next one in eg_plan_order);
 *   DEVELOP    the same mechanic again, a notch harder (the floor after an introduction, or a seen one);
 *   COMBINE    two or more seen mechanics (more often the deeper the floor and the longer the player has played);
 *   TEST       the segment's spike: the hardest mixed room the target allows.
 * The target rides a wave over each three-floor segment before a landing (tension, release, spike; the landing is
 * the rest) and rises with depth and with the player's generation (runs so far; B3's lineage hook). A small,
 * bounded bias per mechanic follows the player: resets in a room make the next room on that mechanic gentler, a
 * fast clean clear makes it harder. A room never repeats the previous floor's trick at the same level, and a room
 * never holds a mechanic the player has not seen (or is being introduced to). Deterministic in the run seed, the
 * seen history and the player's results; on the cartridge it is table lookups. */
#include <stdint.h>
#include "eg_room.h"

/* mechanics: CRTs and drywall are the M0 office everyone starts with; the rest are met through the curriculum */
enum { MK_CRT = 0, MK_DRYWALL = 1, MK_BURN = 2, MK_COLD, MK_SHORT, MK_WEIGHT, MK_GLASS, MK_RHYTHM, MK_STEPS,
       MK_COUNT };
#define MK_BASIC ((uint16_t)((1u << MK_CRT) | (1u << MK_DRYWALL)))
#define MK_TRICK(t) ((uint8_t)(MK_BURN + (t))) /* a room's named trick as a mechanic */
/* seen mechanics live in the save's seen-history word (bits 9-15: BURN .. STEPS, beside SEEN_MITE and friends) */
#define MK_SEEN_SHIFT 7u
#define MK_TO_SEEN(m) ((uint16_t)(((m) & ~MK_BASIC) << MK_SEEN_SHIFT))
#define MK_FROM_SEEN(s) ((uint16_t)((((s) >> MK_SEEN_SHIFT) & 0x1fcu) | MK_BASIC))

enum { ST_INTRO = 0, ST_DEVELOP, ST_COMBINE, ST_TEST };
#define EG_PLAN_BIAS 2 /* the adaptive bias stays within +-2 levels */

/* a baked room: its seed and tier, its trick, mechanics and difficulty vector */
typedef struct {
  uint16_t seed;
  uint8_t tier, trick;
  uint16_t mech;
  uint8_t diff;     /* 3.2's score, clamped to 255; its level is diff / 16 */
  uint8_t dead;     /* dead-end share of the solver's states, % */
  uint8_t fatal;    /* the fewest acts to a fatal mistake (255: none found) */
  uint8_t insights; /* distinct trick types the plan needs */
  uint8_t steps;    /* the plan's length in moves (clamped to 255) */
  uint8_t branch;   /* meaningful acts per state on the plan, x10 */
} eg_seed_rec;
#define EG_ENEMY_PRESSURE(rec) ((uint8_t)(2u + (rec)->tier)) /* placeholder until B2's mobs: the spawn budget */
#define EG_SEEDS 768u
void eg_seed_get(uint16_t k, eg_seed_rec *out) EG_BANKED; /* src/gen/eg_seeds.c */

typedef struct {
  uint16_t seen;       /* MK_* bits the player has met (MK_BASIC always) */
  uint16_t run_seed;
  uint8_t generation;  /* runs so far; B3: the lineage */
  uint8_t lineage;     /* B3 hook: a bloodline's own offset (0 for now) */
  int8_t bias[MK_COUNT];
  uint8_t prev_trick, prev_level, develop; /* develop: the mechanic introduced last floor (255: none) */
  /* the last pick, for the floor's result and for the harness */
  uint8_t stage, target, mech_new;
  uint16_t pick;       /* index into the table */
} eg_plan_state;

extern const uint8_t eg_plan_order[MK_COUNT - 2u]; /* the order mechanics are introduced in */

void eg_plan_begin(eg_plan_state *ps, uint16_t run_seed, uint16_t seen, uint8_t generation) EG_BANKED;
/* the room for floor 1..9: fills *rec and returns the stage */
uint8_t eg_plan_floor(eg_plan_state *ps, uint8_t floor, eg_seed_rec *rec) EG_BANKED;
/* how the floor went: resets used, and frames to clear (0: not cleared) */
void eg_plan_result(eg_plan_state *ps, uint8_t resets, uint16_t frames) EG_BANKED;
/* the floor's wave target (level 0-15) before the bias */
uint8_t eg_plan_target(const eg_plan_state *ps, uint8_t floor) EG_BANKED;

#endif
