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
  T_GATE_OPEN = 12, /* a shutter, up: the player walks through; CRTs don't enter it */
  /* B1 puzzle tiles: what the reaction matrix acts on (src/core/eg_piece.h). THE place the look branch maps from:
   * every cell type id the cartridge draws is in this enum (src/puzzle.c draws them with stand-in art for now). */
  T_WATER = 13,      /* a flood: nobody crosses it; COLD freezes the connected water to ice; it carries a shock */
  T_ICE = 14,        /* frozen flood: floor; HOT thaws the connected ice back to water */
  T_PAPER = 15,      /* a paper and poster barricade: solid; HOT burns it and every paper or plank touching it */
  T_PLANK = 16,      /* a cardboard plank over a hole: floor; it burns with the paper it touches (a pit after) */
  T_ITEM = 17,       /* a piece lying in the room (eg_room.item_*): picked up on entry */
  T_SOCKET = 18,     /* a dead wall socket: a shock into it (or through water touching it) powers it */
  T_SOCKET_ON = 19,  /* a powered socket */
  T_DEAD = 20,       /* a dead shutter: solid until a socket is powered, then it is gone (carpet) */
  T_BLOCK = 21,      /* a laid brick (BRICK used on carpet): solid; it doesn't fall */
  T_BLOCK_PLATE = 22, /* a laid brick on a pressure plate: holds every shutter open */
  T_TYPES = 23
};
#define T_MASK 0x1fu
#define F_FALL 0x80u  /* the CRT moved down or rolled on the last step */
#define F_MOVED 0x40u /* scratch inside eg_gravity */
#define F_PLATE 0x20u /* on a CRT: it rests on a pressure plate (the plate comes back when it leaves) */

#define EG_MAX_SPAWN 6u
#define EG_MAX_ITEM 6u /* = the pocket's size, so the pocket can never be full while an item lies in the room */
#ifdef __SDCC
#include <gb/gb.h>
#define EG_BANKED BANKED
#else
#define EG_BANKED
#endif
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
  /* pieces lying in the room (cells of type T_ITEM; a picked cell turns to carpet, the list stays) */
  uint8_t nitem;
  uint8_t item_cell[EG_MAX_ITEM];
  uint8_t item_piece[EG_MAX_ITEM];
  /* what the generator built (src/core/eg_gen.c): the named trick, supporting tricks (bits), herrings, recipe depth */
  uint8_t trick, support, herrings, depth;
} eg_room;

/* trick types (docs/redesign.md 3.1): the four the office deals in B1 */
enum { TRICK_BURN = 0, TRICK_COLD = 1, TRICK_SHORT = 2, TRICK_WEIGHT = 3, TRICK_COUNT = 4 };

/* Generation. Deterministic in (seed, tier); returns 0 when the layout failed its own checks (the caller reseeds).
 * Solvability is not checked here: the host solver accepts seeds and the cartridge plays only accepted seeds. */
uint8_t eg_generate(eg_room *r, uint16_t seed, uint8_t tier) EG_BANKED;

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
   (t) == T_GATE_OPEN || (t) == T_ICE || (t) == T_PLANK || (t) == T_ITEM)
/* open floor (a whole cell value): walked without changing the board */
#define EG_FLOOR(v) ((v) == T_EMPTY || (v) == T_PLATE || (v) == T_GATE_OPEN || (v) == T_ICE || (v) == T_PLANK)
/* where a CRT may move (fall, roll, be pushed) */
#define EG_ROCK_OPEN(v) ((v) == T_EMPTY || (v) == T_PLATE)

/* Push the resting CRT at `rock` into `to` (a neighbour): into open floor or a pit (which it fills). Returns 0 if
 * `to` can't take it. */
uint8_t eg_push(eg_room *r, uint8_t rock, uint8_t to);
/* The player stepped off `cell`: rotten carpet gives way. Returns 1 if a pit opened. */
uint8_t eg_leave(eg_room *r, uint8_t cell);
/* how many shutters / plates the room has (the generator places at most a few) */
uint8_t eg_count(const eg_room *r, uint8_t type);
/* the piece lying at `cell` (0 if none) */
uint8_t eg_item_at(const eg_room *r, uint8_t cell);

#endif
