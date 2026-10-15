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
/* The slice keeps its four B2 trick bits apart from the legacy STEPS bit.
 * The full-run taxonomy below keeps its existing numeric IDs. */
enum { MK_SLICE_RUSH = MK_COUNT, MK_SLICE_STEER, MK_SLICE_MIRROR, MK_SLICE_STARE, MK_SLICE_COUNT };
#define MK_TRICK(t) ((uint8_t)((t) < TRICK_RUSH ? MK_BURN + (t) : MK_SLICE_RUSH + (t) - TRICK_RUSH)) /* a room's named trick as a mechanic */
/* seen mechanics live in the save's seen-history word (bits 9-15: BURN .. STEPS, beside SEEN_MITE and friends) */
#define MK_SEEN_SHIFT 7u
#define MK_TO_SEEN(m) ((uint16_t)(((m) & 0x1fcu) << MK_SEEN_SHIFT))
#define MK_FROM_SEEN(s) ((uint16_t)((((s) >> MK_SEEN_SHIFT) & 0x1fcu) | MK_BASIC))

enum { ST_INTRO = 0, ST_DEVELOP, ST_COMBINE, ST_TEST };
/* The capped nudge (3.6.8): a fast clean clear makes the next room on that mechanic a notch harder, up to +2. Softening
 * needs repeated failure (EG_PLAN_STREAK failed rooms running on one mechanic), goes at most EG_PLAN_SOFT level below
 * the plan, and never below the curriculum floor for the depth. A clean clear puts a softened mechanic back on plan. */
#define EG_PLAN_HARD 2
#define EG_PLAN_SOFT 1
#define EG_PLAN_STREAK 2
/* the level a room is aimed at: the planned target moved by the bias, never under the floor */
uint8_t eg_nudge_level(uint8_t target, int8_t bias, uint8_t floor_min) EG_BANKED;
/* a room's result on its mechanic's bias and failure streak: failed (2+ resets, or the room was lost), clean (no reset),
 * fast (clean and quick) */
void eg_nudge_result(int8_t *bias, uint8_t *streak, uint8_t failed, uint8_t clean, uint8_t fast) EG_BANKED;

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
  int8_t bias[MK_SLICE_COUNT];
  uint8_t streak[MK_SLICE_COUNT]; /* failed rooms running, per mechanic (the capped nudge) */
  uint8_t prev_trick, prev_level, develop; /* develop: the mechanic introduced last floor (255: none) */
  /* the last pick, for the floor's result and for the harness */
  uint8_t stage, target, mech_new;
  uint16_t pick;       /* index into the table */
} eg_plan_state;

extern const uint8_t eg_plan_order[MK_SLICE_COUNT - 2u]; /* the order mechanics are introduced in */

void eg_plan_begin(eg_plan_state *ps, uint16_t run_seed, uint16_t seen, uint8_t generation) EG_BANKED;
/* the room for floor 1..9: fills *rec and returns the stage */
uint8_t eg_plan_floor(eg_plan_state *ps, uint8_t floor, eg_seed_rec *rec) EG_BANKED;
/* how the floor went: resets used, and frames to clear (0: not cleared) */
void eg_plan_result(eg_plan_state *ps, uint8_t resets, uint16_t frames) EG_BANKED;
/* the floor's wave target (level 0-15) before the bias */
uint8_t eg_plan_target(const eg_plan_state *ps, uint8_t floor) EG_BANKED;

/* ===== The full run (docs/redesign.md 3.6): ~52 floors over 8 biomes =====
 * The B1 cartridge only has the office, so it plays the slice above (eg_plan_floor: nine office floors, then the
 * Watchman). The full run is laid out here, slot by slot, from the run's inputs alone, and the B4 cartridge picks each
 * slot's room from its biome's bank with the same nudge. The host plans and checks it (host/egplan.c).
 *
 * Layout: the 8 biomes in the cycle's order (office or vents first, the glitch core last, each once); per biome 4-6
 * stage slots under the curriculum, the mid-boss after the first half, the biome boss; a rest landing between biomes;
 * after the glitch core's boss, the final stretch and the egress. A run that takes a lineage shortcut door starts at
 * that biome's entrance. Rare stages (3D, glitch rift, sky event) replace combine slots. */
enum { BI_OFFICE = 0, BI_VENTS, BI_ATRIUM, BI_SERVER, BI_MALL, BI_GARDEN, BI_ROOFTOP, BI_CORE, BI_COUNT };
/* the full curriculum: MK_CRT .. MK_STEPS as above, then the B2-B4 puzzle tricks and the living obstacles (7.7) */
enum { MK_KETTLE = MK_COUNT, MK_BEAM, MK_DARK, MK_PATROL, MK_HEAVY, MK_SLEEPER, MK_PUSHER, MK_EATER, MK_SENTRY,
       MK_MIRROR, MK_FOLLOWER, MK_LIGHTEATER, MK_DRIFTER, MK_ALL };
#define MK_OBSTACLE(m) ((m) >= MK_PATROL)
/* slot kinds and stage types (section 4) */
enum { SK_STAGE = 0, SK_MIDBOSS, SK_BOSS, SK_LANDING, SK_FINAL, SK_EGRESS };
enum { SG_PUZZLE = 0, SG_HOLDOUT, SG_CHASE, SG_HUNT, SG_ESCORT, SG_MIXP, SG_DARK, SG_HEIST, SG_3D, SG_RIFT, SG_SKY,
       SG_COUNT };
#define SG_RARE(t) ((t) >= SG_3D)
enum { RM_STORY = 0, RM_DESCENT };
#define EG_RUN_SLOTS 72u
#define EG_DOORS 8u        /* door k: the entrance of the biome at position k (door 0 is the front door) */
#define EG_LAST_DOOR 6u    /* doors 1-6 open when that biome's boss falls; the glitch core has none: it is walked to */
#define EG_INTRO_LEVEL(p) ((uint8_t)(2u + ((p) >> 1))) /* an introduction stays gentle at any depth */

typedef struct {
  uint8_t kind, stype, cstage; /* SK_*, SG_*, ST_* (stages only) */
  uint8_t biome, bpos;         /* BI_* and its position in the run (0-7; descent: the stratum's biome) */
  uint8_t m[3];                /* the slot's mechanics (255: none); INTRO and DEVELOP use m[0] alone */
  uint8_t twist;               /* a 3.3 floor theme on top (0: none, 1-7: PLACE .. ELEMENT) */
  uint8_t target;              /* the planned level (0-15) */
  uint8_t floor_min;           /* the curriculum floor: no nudge goes under it */
  uint8_t floor;               /* the floor number shown (landings share the floor before them) */
} eg_slot;

typedef struct {
  uint16_t run_seed, cycle_seed; /* the cycle seed fixes the biome order and stage counts for the line */
  uint32_t seen;                 /* MK bits the line has been introduced to before this run */
  uint32_t developed;            /* ... and has also played the develop room of (only these join a mix) */
  uint8_t generation, lineage;   /* runs so far; the bloodline's own offset */
  uint8_t door;                  /* the shortcut taken: the biome position the run starts at (0: the front door) */
  uint8_t cycle;                 /* 1: the first world; each new cycle reassembles it, harder */
  uint8_t mode;                  /* RM_STORY, or RM_DESCENT after the first egress */
  uint8_t stratum;               /* descent only: which stretch of the endless mode this layout covers */
  uint8_t sky;                   /* a sky event is active today (19): one sky stage is dealt */
} eg_run_in;

typedef struct {
  uint8_t n, floors;           /* slots, and floors (a landing is not a floor) */
  uint8_t order[BI_COUNT];     /* biome by position */
  uint8_t stages[BI_COUNT];    /* stage slots per position */
  uint8_t band[MK_ALL];        /* the biome position each mechanic arrives at (255: basic) */
  eg_slot s[EG_RUN_SLOTS];
} eg_run;

/* the cycle's world: biome order, stage counts and mechanic bands (fixed until a new cycle) */
void eg_run_world(eg_run *r, uint16_t cycle_seed, uint8_t cycle) EG_BANKED;
/* the whole layout of one run (story) or one descent stratum */
void eg_run_build(eg_run *r, const eg_run_in *in) EG_BANKED;

#endif
