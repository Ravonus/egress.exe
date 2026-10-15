#ifndef PUZZLE_H
#define PUZZLE_H
/* B1 puzzle rooms on the cartridge (src/puzzle.c): the pocket, the bench overlay, using pieces, reaction tiles. */
#include <gb/gb.h>
#include <stdint.h>
#include "core/eg_room.h"

extern uint8_t pocket[EG_MAX_ITEM], pocket_n; /* insertion order */
extern uint8_t bench_on, bench_cur, last_rx;
void pocket_reset(void) BANKED;
void puzzle_pick(uint8_t cell) BANKED; /* the player walked into a T_ITEM cell */
void puzzle_draw(uint8_t i) BANKED;    /* draws cells of type T_CRUMBLE and up */
void bench_run(void) BANKED;           /* SELECT tapped: the bench, modal, until closed or a piece is used */

/* fragile floor, the dead-room hint and the run planner */
#include "core/eg_plan.h"
extern uint8_t puzzle_dirty;  /* set after a break or a push: puzzle_tick looks for a dead room */
extern uint8_t frag_skip;     /* B3: a blink sets it, and the step it makes doesn't wear the floor */
extern uint8_t floor_resets;
extern eg_plan_state plan;
void puzzle_tick(void) BANKED;
uint8_t puzzle_frag_hit(uint8_t cell, uint8_t who) BANKED; /* B2 mobs: break fragile floor (refused if protected) */
void look_collapse(uint8_t cell) BANKED;                   /* b1-look replaces this with its per-tile collapse */
void puzzle_plan_begin(void) BANKED;
void puzzle_plan_floor(void) BANKED; /* sets floor_seed and floor_tier */
void puzzle_plan_done(void) BANKED;
void puzzle_reset_cost(void) BANKED;

#endif
