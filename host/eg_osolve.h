#ifndef EG_OSOLVE_H
#define EG_OSOLVE_H
/* Host tick solver for rooms with living obstacles (docs/redesign.md 7.7; the rules: src/core/eg_obst.c).
 *
 * One step is one room tick: the player acts (a cardinal move, a push, a turn in place, or a wait), the shutters
 * follow the plates, then eg_ob_tick runs the obstacles and the shutters follow again. The solver explores EVERY
 * reachable state (up to a cap), sweeps back from the wins for the winnable set, and measures the room's fairness:
 *   1 the start is winnable;
 *   2 the idle rollout: waiting from the start never leaves the winnable set (and never costs a hit early on);
 *   3 every state on the plan is passive-safe: waiting there, any number of ticks, never loses the room. A winnable
 *     state that one more wait loses is a rescue window; they are counted.
 * The plan is the cleanest passive-safe win: fewest hits, then fewest ticks, then fewest moves.
 *
 * Plan alphabet: U D L R (move / push / collect / enter the door), '.' (wait a tick), n e s w (turn in place: a tap
 * of the d-pad that doesn't leave the cell; only in rooms with a sentry). */
#include <stddef.h>
#include <stdint.h>
#include "../src/core/eg_room.h"
#include "../src/core/eg_obst.h"

#define EGO_NOISE 1   /* the player's glass breaks are noise (lures, wakes): the cartridge's rule */
#define EGO_ORDERED 2 /* crates keep their identity in the state (the mock room's model); else they are canonical */

typedef struct {
  int states, live, dead; /* reachable states, winnable ones, the rest */
  int capped;             /* the exploration hit the cap (the room is rejected) */
  int start_live;         /* fairness 1 */
  int idle_ok, idle_hits; /* fairness 2: waiting from the start stays winnable; hits taken in the rollout window */
  int plan_ok;            /* a passive-safe winning plan exists (fairness 3) */
  int ticks, moves, hits; /* the plan's length in ticks, its non-wait actions, its hits */
  int rescue;             /* rescue-window states (winnable; one more wait loses the room) */
  int fatal_depth;        /* the first tick at which some play is in a losing state (-1: never) */
  int passive;            /* passive-safe states */
} eg_ostats;

/* returns 1 when the room has a passive-safe winning plan (written to plan, NUL-terminated) */
int eg_osolve(const eg_room *r, int cap, int flags, char *plan, size_t plancap, eg_ostats *st);
/* replays a plan with the same rules: 1 when it ends in the door; *hits gets the hits taken */
int eg_oreplay(const eg_room *r, const char *plan, int flags, int *hits);
/* the idle rollout length: two full obstacle cycles (2 * 4 * period * (W + H) ticks) */
#define EGO_IDLE_TICKS (2 * 4 * 2 * (16 + 14))

#endif
