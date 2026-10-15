#ifndef EG_STAGE_H
#define EG_STAGE_H
/* B2 stage types (docs/redesign.md 4): the rules every non-puzzle stage plays by, its generator and score, and the
 * weighted deck that deals stage types with no back-to-back repeat and a seen history. Shared verbatim by the
 * cartridge (src/stage.c) and the host stage sim (host/egstage.c, `egplan stages`).
 *
 * A stage room is the same 16x14 eg_room as a puzzle room (outer ring of wall = the void, the egress door in the north
 * wall), built by eg_stage_gen for one stage type at one target level. What a stage adds beyond the room is the small
 * eg_stage record below (the ring, the waves, the chase speed, the hunt target and its conditions, the escort's
 * waypoints, the recipe to make, the shop). Every generated stage is checked on the spot (a reachability flood over
 * a bit table, no queue): the cartridge never plays a stage whose exit, ring, items or path can't be reached.
 *
 * Portable C for SDCC (SM83) and host compilers: 8/16-bit arithmetic, no recursion, no allocation. */
#include <stdint.h>
#include "eg_room.h"
#include "eg_plan.h"

/* the stage types the deck deals (SG_* of eg_plan.h): SG_PUZZLE .. SG_HEIST; rare stages are the planner's */
#define EG_DECK_TYPES SG_3D
#define EG_DECK_HIST 6u   /* the last 6 types dealt */
#define EG_DECK_TRICKS 10u /* the last 10 tricks played */

/* win conditions */
enum { WIN_EXIT = 0, /* reach the open door */
       WIN_CHARGE,   /* holdout: the egress charges while you stand in its ring; waves come */
       WIN_OUTRUN,   /* chase: reach the door before the floor that drops behind you */
       WIN_KILL,     /* hunt: the marked target dies by its condition */
       WIN_ESCORT,   /* the temp reaches the door alive */
       WIN_MAKE,     /* mix under pressure: the recipe is made */
       WIN_KEYS,     /* dark room: the keycards, then the door */
       WIN_LEAVE };  /* heist: pay or take, then get out (taking it starts a holdout) */

/* hunt kill conditions (7.3): which hit counts. The cartridge maps them onto b2-mobs' defences (MD_CRUSH,
 * MD_REFLECT, MD_ARMOR) through src/stage_spawn.h */
enum { KC_CRUSH = 0, /* only a falling CRT: lure it under one */
       KC_ORB,       /* only its own orb, returned with the lash */
       KC_BACK,      /* only a hit from behind, right after a dodge roll */
       KC_COUNT };

#define EG_STAGE_PATH 8u
typedef struct {
  uint8_t type, level;   /* SG_*, the planned level it was built for (0-15) */
  uint8_t score;         /* the generator's own level for what it built (0-15): the band test */
  uint8_t budget;        /* enemy budget: the sum of mob cost over the whole stage */
  uint8_t conds;         /* condition count: kill and gate conditions to meet */
  uint8_t waves;         /* holdout, heist on a theft: 2-4; escort, mixp: the waves that come while it lasts */
  uint8_t per_wave;      /* mobs a wave, at most 3 alive (the 10-per-line budget) */
  uint8_t shooters;      /* waves from this index on bring one shooter */
  uint8_t ring, ring_r;  /* the ring's centre cell and Chebyshev radius (holdout, mixp) */
  uint16_t charge;       /* holdout: frames in the ring to full charge */
  uint8_t speed;         /* chase: frames per row of the drop; escort: frames per cell walked */
  uint8_t grace;         /* chase: frames before the floor starts to drop, x4 */
  uint8_t piece;         /* mixp: the recipe to make; heist: the goods; dark: the light */
  uint8_t cond[2];       /* hunt: the target's kill conditions, in order (KC_*; 255 none) */
  uint8_t tcell;         /* hunt: the target's cell; heist: the shopkeeper's cell; escort: the temp's start */
  uint8_t price;         /* heist: tokens */
  uint8_t spot;          /* heist: the goods' cell; mixp: the bench's cell */
  uint8_t npath, path[EG_STAGE_PATH]; /* escort: waypoints from the start to the cell under the door */
  uint8_t lamps;         /* dark: lamps placed (GLOWS objects, T_BLOCK cells listed in path[]) */
} eg_stage;

/* the per-type rules: the HUD objective (<= 18 characters, section 12), the win condition, and whether the door is
 * open from the first frame */
/* the objective of type t (which < EG_DECK_TYPES) or the hunt tell of KC k (which = EG_DECK_TYPES + k), copied into
 * out (19 bytes): the strings live in this module's bank */
void eg_stage_text(uint8_t which, char *out) EG_BANKED;
uint8_t eg_stage_win(uint8_t type) EG_BANKED;

/* Build a stage room. Deterministic in (type, seed, level, m); `m` is the slot's first mechanic (mixp: the trick
 * whose object it must make; 255 none). Rerolls internally (the seed walks) until the stage scores inside the band
 * (|score - level| <= 1) or the tries run out (the closest so far is kept). Returns the seed it settled on (never 0
 * when a stage was made; 0 only if no layout passed its own checks, which the host sim shows never happens). */
uint16_t eg_stage_gen(eg_room *r, eg_stage *s, uint8_t type, uint16_t seed, uint8_t level, uint8_t m) EG_BANKED;
#define EG_STAGE_TRIES 8u
#define EG_CHASE_WALK 16u /* frames a cell the chase assumes the player walks (they walk ~11: the slack is real) */

/* the reachability flood the generator checks itself with (and the host sim re-checks): from `from`, through cells
 * EG_PASSABLE (the door counts as open); fills eg_stage_reach. Returns the number of cells reached. */
extern uint8_t eg_stage_reach[EG_N / 8u];
#define EG_REACHED(i) (eg_stage_reach[(uint8_t)(i) >> 3] & (uint8_t)(1u << ((i) & 7u)))
uint8_t eg_stage_flood(const eg_room *r, uint8_t from) EG_BANKED;

/* mob costs for the budget (7.4: a chaser 2, a shooter 3, a hunt target 4 + 2 per condition) */
#define EG_COST_CHASER 2u
#define EG_COST_SHOOTER 3u

/* ===== the deck (4, 3.6.2) ===== */
typedef struct {
  uint8_t hist[EG_DECK_HIST];     /* newest first; 255 empty */
  uint8_t tricks[EG_DECK_TRICKS]; /* newest first; 255 empty */
} eg_deck;
void eg_deck_reset(eg_deck *d) EG_BANKED;
/* Weights per slot: puzzle 5, holdout 2, chase 2 and hunt 2 (from biome position 1), escort 1 and mix under pressure
 * 2 (from 2), dark 2 (once lights-out is open), heist 1 (from 3, never a test). An introduction is always a puzzle;
 * a develop deals what its mechanic allows (3.6.2). The history: never the last type again (puzzles excepted: a
 * puzzle is the default room), and every other non-puzzle type seen in the last 6 deals at half weight per sighting.
 * `roll` is 0-255 from the run seed and the slot. */
uint8_t eg_deck_deal(const eg_deck *d, uint8_t bpos, uint8_t cstage, uint8_t m0, uint8_t dark_open, uint8_t roll)
    EG_BANKED;
void eg_deck_note(eg_deck *d, uint8_t type, uint8_t trick) EG_BANKED; /* trick: 255 when the stage had none */
uint8_t eg_deck_trick_seen(const eg_deck *d, uint8_t trick) EG_BANKED; /* in the last 10: 1 + its age, else 0 */

/* ===== the cartridge's run (the B1 slice, B2): nine office floors, a mid-boss slot, the boss ===== */
#define EG_SLICE_MIDBOSS 5u /* the mid-boss slot: after the first half (3.6.1), between the first two landings */
/* the slice's floors stand in for biome positions, so later floors open the later stage types (3.6.2) */
#define EG_SLICE_POS(floor) ((uint8_t)(((floor) - 1u) >> 1))
/* the deal's roll for a floor */
uint8_t eg_stage_roll(uint16_t run_seed, uint8_t floor, uint8_t salt) EG_BANKED;
/* the mid-boss slot's level: the planner's wave target for the slot, + 2 (3.6.2) */
#define EG_MIDBOSS_LEVEL(target) ((uint8_t)((target) + 2u > 15u ? 15u : (target) + 2u))
typedef struct {
  eg_deck deck;
  uint8_t kind;   /* SK_STAGE, SK_MIDBOSS, SK_BOSS */
  uint8_t type;   /* SG_*: what the floor plays */
  uint8_t cstage; /* ST_*: the curriculum's slot (stages) */
  uint8_t level;  /* the planned level (0-15) */
  uint8_t m;      /* the slot's mechanic (the baked room's trick as MK_*), 255 none; 254: the mid-boss (elite) */
  uint8_t planned; /* the run planner chose this floor's room (its result is reported back) */
} eg_slice;
void eg_slice_begin(eg_slice *sl) EG_BANKED;
/* The floor's slot on the cartridge's run: floors 1..last-1 are curriculum stages (the planner picks the baked room;
 * the deck deals the type), EG_SLICE_MIDBOSS is the mid-boss slot (the boss branch's arena; a hunt with an elite
 * target until it merges), the last floor the boss. `force` (255: none) forces a stage type (the harness). Fills
 * *rec for planned floors. */
uint8_t eg_slice_floor(eg_slice *sl, eg_plan_state *ps, uint8_t floor, uint8_t last, eg_seed_rec *rec, uint8_t force)
    EG_BANKED;

#endif
