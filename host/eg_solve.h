#ifndef EG_SOLVE_H
#define EG_SOLVE_H
/* Host-only room solver. Best-first search over settled boards: the player walks freely inside its open region, and
 * a search step is one board-changing act (dig, collect, push, break, or leave a cell that holds a CRT up). Any plan
 * it returns is replayed and checked with the same eg_gravity the cartridge runs, so an accepted seed is solvable;
 * a seed it cannot solve within its budget is simply rejected. */
#include <stddef.h>
#include "../src/core/eg_room.h"

typedef struct {
  int actions;  /* board-changing acts */
  int steps;    /* primitive moves (walking included) */
  int pushes, breaks, digs, drops; /* drops: acts after which CRTs fell or rolled */
  int nodes;    /* search nodes expanded */
} eg_stats;

/* moves: 'U' 'D' 'L' 'R' step (dig, collect, push, walk), 'u' 'd' 'l' 'r' strike the drywall in that direction.
 * Returns 1 when solved (moves NUL-terminated, the last move enters the open door), 0 otherwise. */
int eg_solve(const eg_room *r, int budget, char *moves, size_t cap, eg_stats *st);

/* 1 when every keycard and the door are reachable without moving or breaking anything (no puzzle). */
int eg_trivial(const eg_room *r);

/* 1 when concrete alone cuts a keycard or the door off from the start (no search can help): a cheap reject. */
int eg_sealed(const eg_room *r);

/* Replays moves on the grid model: returns 1 when they end inside the open door without the player being hit. */
int eg_replay(const eg_room *r, const char *moves);

#endif
