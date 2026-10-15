#ifndef EG_ROOM_H
#define EG_ROOM_H
/* EGRESS room rules, shared by the cartridge and the host tools (solver, seed bake, tests).
 *
 * A floor is a 16x14 grid of cells (256x224 px at 16 px per cell; it fits the CGB's 32x32 background map whole, so
 * the game only scrolls). Boulder Dash physics: CRTs (the "rocks") fall into empty cells, roll off other resting CRTs,
 * and can be pushed sideways into an empty cell. A falling CRT that lands on the player hurts; a resting CRT above the
 * player is held up. Foam (the "dirt") is dug by walking into it. Keycards open the egress door in the north wall.
 *
 * Portable C for SDCC (SM83) and host compilers: 8/16-bit arithmetic only, no recursion, no allocation. */
#include <stdint.h>

#define EG_W 16u
#define EG_H 14u
#define EG_N 224u /* EG_W * EG_H; cell indices fit a uint8_t */
#define EG_IDX(x, y) ((uint8_t)((uint8_t)(y) * EG_W + (uint8_t)(x)))
#define EG_X(i) ((uint8_t)((i) & 15u))
#define EG_Y(i) ((uint8_t)((i) >> 4))

enum {
  T_EMPTY = 0, /* carpet */
  T_DIRT = 1,  /* insulation foam: dug on entry */
  T_WALL = 2,  /* wallpapered concrete */
  T_BRICK = 3, /* drywall: broken by the weapon */
  T_ROCK = 4,  /* a CRT set: falls, rolls, pushed sideways */
  T_TOKEN = 5, /* an arcade token: collected on entry, like foam */
  T_KEY = 6,   /* a keycard: collected on entry */
  T_DOOR = 7,  /* the egress door: solid until every keycard is held */
  T_CRUMBLE = 8, /* rotten carpet: holds you once; it gives way to a pit when you step off */
  T_PIT = 9,     /* a hole: nobody crosses it; a CRT that drops or is pushed in fills it (carpet again) */
  T_PLATE = 10,  /* a pressure plate: floor; a CRT resting on it opens every shutter in the room */
  T_GATE = 11,   /* a shutter, down: solid */
  T_GATE_OPEN = 12 /* a shutter, up: the player walks through; CRTs don't enter it */
};
#define T_MASK 0x0fu
#define F_FALL 0x80u  /* the CRT moved down or rolled on the last step */
#define F_MOVED 0x40u /* scratch inside eg_gravity */
#define F_PLATE 0x20u /* on a CRT: it rests on a pressure plate (the plate comes back when it leaves) */

#define EG_MAX_SPAWN 6u
enum { SPAWN_MITE = 0, SPAWN_EYE = 1 };

typedef struct {
  uint8_t c[EG_N];
  uint8_t start;  /* the player's first cell */
  uint8_t door;   /* the egress door's cell (north wall) */
  uint8_t keys;   /* keycards placed */
  uint8_t tokens; /* tokens placed */
  uint8_t nspawn;
  uint8_t spawn[EG_MAX_SPAWN];
  uint8_t spawn_kind[EG_MAX_SPAWN];
} eg_room;

/* Generation. Deterministic in (seed, tier); returns 0 when the layout failed its own checks (the caller reseeds).
 * Solvability is not checked here: the host solver accepts seeds and the cartridge plays only accepted seeds. */
uint8_t eg_generate(eg_room *r, uint16_t seed, uint8_t tier);

/* One gravity step. occ[0..nocc) are cells the player's body covers: a resting CRT does not move into them, a
 * falling one stops and reports a hit. Returns EG_G_* flags. Cells that changed are listed in eg_dirty. */
#define EG_G_CHANGED 1u
#define EG_G_HIT 2u
#define EG_G_FALLING 4u /* some CRT is still falling or rolling */
uint8_t eg_gravity(eg_room *r, const uint8_t *occ, uint8_t nocc);

#define EG_DIRTY_MAX 48u
extern uint8_t eg_dirty[EG_DIRTY_MAX];
extern uint8_t eg_ndirty; /* > EG_DIRTY_MAX: overflowed, redraw everything */

/* xorshift16, the game's own stream */
extern uint16_t eg_rs;
uint8_t eg_rnd(void);
uint8_t eg_rndn(uint8_t n); /* 0..n-1 */

/* Cell kinds a body may stand in */
#define EG_PASSABLE(t)                                                                                              \
  ((t) == T_EMPTY || (t) == T_DIRT || (t) == T_TOKEN || (t) == T_KEY || (t) == T_CRUMBLE || (t) == T_PLATE ||       \
   (t) == T_GATE_OPEN)
/* open floor (a whole cell value): walked without changing the board */
#define EG_FLOOR(v) ((v) == T_EMPTY || (v) == T_PLATE || (v) == T_GATE_OPEN)
/* where a CRT may move (fall, roll, be pushed) */
#define EG_ROCK_OPEN(v) ((v) == T_EMPTY || (v) == T_PLATE)

/* Push the resting CRT at `rock` into `to` (a neighbour): into open floor or a pit (which it fills). Returns 0 if
 * `to` can't take it. */
uint8_t eg_push(eg_room *r, uint8_t rock, uint8_t to);
/* The player stepped off `cell`: rotten carpet gives way. Returns 1 if a pit opened. */
uint8_t eg_leave(eg_room *r, uint8_t cell);
/* how many shutters / plates the room has (the generator places at most a few) */
uint8_t eg_count(const eg_room *r, uint8_t type);

#endif
