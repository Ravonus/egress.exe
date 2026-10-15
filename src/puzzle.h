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

#endif
