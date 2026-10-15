#ifndef EG_SOLVE_H
#define EG_SOLVE_H
/* Host-only room solver. Best-first search over settled boards: the player walks freely inside its open region, and
 * a search step is one board- or pocket-changing act: dig, collect, pick up a piece, push, break, leave a cell that
 * holds a CRT up, combine two pieces on the bench, or use a piece on a neighbouring cell (the reaction matrix, through
 * the shared eg_apply). Any plan it returns is replayed and checked with the same eg_gravity and eg_apply the
 * cartridge runs, so an accepted seed is solvable; a seed it cannot solve within its budget is simply rejected. */
#include <stddef.h>
#include <stdint.h>
#include "../src/core/eg_room.h"
#include "../src/core/eg_piece.h"
#include "../src/core/eg_plan.h"

#define EG_MAX_ENT 4 /* B2: living obstacles per room in the solver state */

typedef struct {
  int actions;  /* board- or pocket-changing acts */
  int steps;    /* primitive moves (walking included; a bench act counts one) */
  int pushes, breaks, digs, drops; /* drops: acts after which CRTs fell or rolled */
  int picks, combines, uses;       /* pieces picked up, bench recipes made, pieces used on cells */
  uint64_t reactions;              /* bit per RX_* the plan sets off */
  int branching100;                /* mean meaningful acts available per state on the plan, x100 */
  int nodes;    /* search nodes expanded */
  int states;   /* distinct states generated */
  int dead;     /* of those, dead ends (eg_dead): unforgiving rooms have many */
  int fatal_depth; /* the fewest acts after which a fatal mistake was found (-1: none found) */
  int dead_at_start; /* EG_DEAD_* when the room is unwinnable before the player acts (a fault, not a mistake) */
  /* B2 rooms with living obstacles are solved tick by tick (host/eg_osolve.c); these come from there */
  int ob_rooms;       /* 1: this room has obstacles (the fields below are set) */
  int ob_capped;      /* the exploration hit EG_OB_CAP */
  int ob_start_live, ob_idle_ok, ob_idle_hits, ob_plan_ok; /* fairness 1-3 */
  int ob_ticks, ob_hits, ob_rescue;
} eg_stats;
/* B2: the tick solver's state cap per room (7.7: rooms over 4M states are rejected) */
#ifndef EG_OB_CAP
#define EG_OB_CAP 4000000
#endif

/* moves:
 *   'U' 'D' 'L' 'R'   step (dig, collect, pick up, push, walk)
 *   'u' 'd' 'l' 'r'   strike the drywall in that direction
 *   'C' a b           combine pieces a and b on the bench (piece ids as letters: 'a' + id - 1)
 *   'P' a D           use piece a on the neighbouring cell in direction D (U D L R)
 * Returns 1 when solved (moves NUL-terminated, the last move enters the open door), 0 otherwise. */
int eg_solve(const eg_room *r, int budget, char *moves, size_t cap, eg_stats *st);
/* the same, with the reactions in `ban` (bit per RX_*) never set off: the trick-necessity check */
int eg_solve_ban(const eg_room *r, int budget, uint64_t ban, char *moves, size_t cap, eg_stats *st);

/* 1 when every keycard and the door are reachable without moving or breaking anything (no puzzle). */
int eg_trivial(const eg_room *r);

/* 1 when concrete alone cuts a keycard or the door off from the start (no search can help): a cheap reject. */
int eg_sealed(const eg_room *r);
/* the same, with cells of `type` counted as concrete too */
int eg_sealed_with(const eg_room *r, uint8_t type);

/* trick necessity: 1 when the room can't be solved without its named trick's reaction (a sound proof: the trick's
 * obstacle counted as concrete seals the goal off, and nothing but that reaction removes it) */
int eg_trick_needed(const eg_room *r);

/* herring safety (single misuse): every recipe a red herring makes with another piece in the room, made and thrown
 * away at the start, still leaves the room solvable. Returns 1 when safe; *bad gets the first unsafe pair. */
int eg_herrings_safe(const eg_room *r, int budget, int *bad_a, int *bad_b);

/* Replays moves on the grid model: returns 1 when they end inside the open door without the player being hit. */
int eg_replay(const eg_room *r, const char *moves);

/* 1 when every glitched-glass run is at most EG_GLITCH_RUN cells (crossable inside one beat) */
int eg_glitch_ok(const eg_room *r);

/* the mechanics the room uses (MK_* bits, src/core/eg_plan.h) and its distinct insights (trick types needed) */
uint16_t eg_room_mech(const eg_room *r);
int eg_insights(const eg_room *r);

/* the score of 3.2: steps + 3 branching + 4 depth + 2 supporting tricks. B2 obstacle rooms: ticks + 6 x the
 * obstacle cost (7.7) + the rescue windows (one point per 50, at most 30) + 2 supporting obstacles */
int eg_difficulty(const eg_room *r, const eg_stats *st);
/* B2: the obstacle load of 7.7 (the sum of the obstacles' costs, plus one per 100 rescue windows, at most 10) */
int eg_obstacle_load(const eg_room *r, const eg_stats *st);

extern const char *const eg_trick_name[TRICK_COUNT];

#endif
