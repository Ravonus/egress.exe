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
  T_GLITCH = 14,     /* glitched glass (fragile kind FK_GLITCH): floor on the beat, void off it (eg_beat_on) */
  T_PAPER = 15,      /* a paper and poster barricade: solid; HOT burns it and every paper or plank touching it */
  T_PLANK = 16,      /* a cardboard plank over a hole: floor; it burns with the paper it touches (a pit after) */
  T_ITEM = 17,       /* a piece lying in the room (eg_room.item_*): picked up on entry */
  T_SOCKET = 18,     /* a dead wall socket: a shock into it (or through water touching it) powers it */
  T_SOCKET_ON = 19,  /* a powered socket */
  T_DEAD = 20,       /* a dead shutter: solid until a socket is powered, then it is gone (carpet) */
  T_BLOCK = 21,      /* a laid brick (BRICK used on carpet): solid; it doesn't fall */
  T_BLOCK_PLATE = 22, /* a laid brick on a pressure plate: holds every shutter open */
  /* fragile floor (eg_frag below): a kind's crack stages are consecutive ids, stage = base + steps taken; the step
   * that would reach `steps` leaves a pit instead. The look branch draws hairline -> split per stage. */
  T_WORN = 23,   /* worn carpet tile, 3 steps */
  T_WORN_1 = 24,
  T_WORN_2 = 25,
  T_CEIL = 26,   /* a ceiling tile laid as floor, 2 steps */
  T_CEIL_1 = 27,
  T_GLASS = 28,  /* glass over the void, 1 step: see-through */
  T_ICE = 29,    /* thin ice (COLD froze a flood), 2 steps; HOT thaws the connected ice back to water */
  T_ICE_1 = 30,
  T_PARTITION = 31, /* B2: an office glass partition: solid; only a heavy obstacle breaks it, walking in (eg_obst.h) */
  T_TYPES = 32
};
#define T_MASK 0x1fu
#define F_FALL 0x80u  /* the CRT moved down or rolled on the last step */
#define F_MOVED 0x40u /* scratch inside eg_gravity */
#define F_PLATE 0x20u /* on a CRT: it rests on a pressure plate (the plate comes back when it leaves) */
#define F_OB 0x80u    /* B2, on any cell but a CRT: a living obstacle stands here (src/core/eg_obst.h). Solid to the
                         player, crates and other obstacles; masked off wherever the cell's type is read */

#define EG_MAX_SPAWN 6u
#define EG_MAX_OB 3u   /* B2: living obstacles per room (7.7: at most 3 slots, at most 2 free movers) */
#define EG_MAX_RUN 8u  /* B2: route runs per room (one patroller loop) */
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
  /* protected cells (bit per cell): the landing, the door threshold, both sides of every lock opening, the trick's
   * apparatus, every piece and keycard. Never fragile, never broken by anything but the player's own plan. */
  uint8_t prot[EG_N / 8u];
  /* B2 living obstacles (docs/redesign.md 7.7, src/core/eg_obst.h): the baked records (4 bytes each) and the room's
   * route table (one byte per run: direction 2 bits | length 6 bits). Appended: the B1 layout above is unchanged. */
  uint8_t nob;
  uint8_t ob[EG_MAX_OB][4];
  uint8_t nrun;
  uint8_t run[EG_MAX_RUN];
} eg_room;
#define EG_PROT(r, i) ((r)->prot[(uint8_t)(i) >> 3] & (uint8_t)(1u << ((i) & 7u)))

/* trick types (docs/redesign.md 3.1): the four the office deals in B1 */
enum { TRICK_BURN = 0, TRICK_COLD = 1, TRICK_SHORT = 2, TRICK_WEIGHT = 3, /* the reaction tricks */
       TRICK_GLASS = 4,  /* one-way glass: the top lock is the void with a glass bridge; every glass cell takes one
                            crossing, and the keycard lies behind you: take what you need before you cross */
       TRICK_RHYTHM = 5, /* glitched glass: the top lock is a band of void with a glitched-glass bridge, floor only
                            on the beat: cross on the beat (at most EG_GLITCH_RUN cells in one beat) */
       TRICK_COUNT = 6 };
#define EG_GLITCH_RUN 3u /* the longest glitched-glass run a room may hold: crossed inside one on-beat */

/* Generation. Deterministic in (seed, tier); returns 0 when the layout failed its own checks (the caller reseeds).
 * Solvability is not checked here: the host solver accepts seeds and the cartridge plays only accepted seeds. */
uint8_t eg_generate(eg_room *r, uint16_t seed, uint8_t tier) EG_BANKED;

/* One gravity step. occ[0..nocc) are cells the player's body covers: a resting CRT does not move into them, a
 * falling one stops and reports a hit. Returns EG_G_* flags. Cells that changed are listed in eg_dirty. */
#define EG_G_CHANGED 1u
#define EG_G_HIT 2u
#define EG_G_FALLING 4u /* some CRT is still falling or rolling */
#define EG_G_BROKE 8u   /* a falling CRT broke through fragile floor (run eg_dead) */
uint8_t eg_gravity(eg_room *r, const uint8_t *occ, uint8_t nocc);
/* The cartridge runs eg_gravity's shutter pass (plates -> gates) only when a plate's load may have changed: set by
 * room generation, any eg_apply (a brick), and a CRT coming onto or off a plate (eg_push, falls, rolls, breaks); kept
 * set while a shutter waits for the player to leave its cell. The host always runs the pass. */
extern uint8_t eg_plates_dirty;

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
   (t) == T_GATE_OPEN || (t) == T_PLANK || (t) == T_ITEM || (t) == T_GLITCH || ((t) >= T_WORN && (t) <= T_ICE_1))
/* open floor (a whole cell value): walked without changing the board (fragile cells change it: see eg_frag) */
#define EG_FLOOR(v) ((v) == T_EMPTY || (v) == T_PLATE || (v) == T_GATE_OPEN || (v) == T_PLANK || (v) == T_GLITCH)
#define EG_FRAGILE(t) ((t) == T_CRUMBLE || ((t) >= T_WORN && (t) <= T_ICE_1))
/* where a CRT may move (fall, roll, be pushed) */
#define EG_ROCK_OPEN(v) ((v) == T_EMPTY || (v) == T_PLATE)

/* Push the resting CRT at `rock` into `to` (a neighbour): into open floor or a pit (which it fills). Returns 0 if
 * `to` can't take it. */
uint8_t eg_push(eg_room *r, uint8_t rock, uint8_t to);
/* B2: push the crate (a laid brick: T_BLOCK, or T_BLOCK_PLATE on a plate) at `from` into the neighbour `to`, Sokoban
 * rules, by the player, a pusher or a mirror: onto carpet or a plate (T_BLOCK_PLATE: it weighs the plate); onto floor
 * glass or a ceiling tile it smashes through (a pit, the crate gone). Anything else, a pit included, refuses (0). */
#define EG_CRATE(v) ((v) == T_BLOCK || (v) == T_BLOCK_PLATE)
uint8_t eg_push_block(eg_room *r, uint8_t from, uint8_t to);
/* The player stepped off `cell`: rotten carpet gives way. Returns 1 if a pit opened. */
uint8_t eg_leave(eg_room *r, uint8_t cell);
/* how many shutters / plates the room has (the generator places at most a few) */
uint8_t eg_count(const eg_room *r, uint8_t type);
/* the piece lying at `cell` (0 if none) */
uint8_t eg_item_at(const eg_room *r, uint8_t cell);

/* --- fragile floor (docs/redesign.md 3.4): a data-driven family. Each kind: its first cell id (stage 0), steps to
 * break (0: steps don't wear it), a reset mode, what may break it, and look flags. The crack stage lives in the cell
 * id itself, so per-cell runtime state costs nothing; a timed reset keeps a short list (eg_frag_tick). */
enum { FK_CRUMBLE = 0, FK_WORN, FK_CEIL, FK_GLASS, FK_GLITCH, FK_ICE, FK_COUNT, FK_NONE = 255 };
enum { FR_NONE = 0, FR_TIMED, FR_ROOM }; /* reset: never, after `reset_ticks` x 8 frames, when the room resets */
#define BB_PLAYER 0x01u       /* a player step (counts 1; HEAVY counts 2) */
#define BB_HEAVY 0x02u        /* a heavy body: the HEAVY trait (B3), a heavy object */
#define BB_MOB_WALKER 0x04u   /* B2 mobs */
#define BB_MOB_HEAVY 0x08u
#define BB_MOB_BURROWER 0x10u
#define BB_BOSS 0x20u
#define BB_FALLING 0x40u      /* a falling or pushed CRT */
#define BB_REACTION 0x80u     /* the reaction matrix (fire through wood) */
#define FF_SEE 0x01u          /* see-through: glass shows the void below */
#define FF_PHASED 0x02u       /* glitched: solid only on the beat */
typedef struct {
  uint8_t base, steps, reset, reset_ticks, breakby, flags;
} eg_frag_kind;
extern const eg_frag_kind eg_frag[FK_COUNT];
uint8_t eg_frag_kind_of(uint8_t type); /* FK_* or FK_NONE */
/* A body of kind `who` (BB_*) stepped off `cell`, `weight` steps' worth (a player 1, HEAVY 2; a blink calls
 * nothing). Returns 1 when the cell broke (it is a pit now). eg_leave is this for the player. */
uint8_t eg_step_off(eg_room *r, uint8_t cell, uint8_t who, uint8_t weight);
/* Anything else breaking a fragile cell outright (a mob, a falling CRT, the boss): refused on protected cells and
 * for kinds `who` can't break. Returns 1 when it broke. The B2 mob interface. */
uint8_t eg_frag_hit(eg_room *r, uint8_t cell, uint8_t who);
/* the glitch beat: 128-frame period, floor for the first EG_BEAT_ON frames of it */
#define EG_BEAT_LEN 128u
#define EG_BEAT_ON 80u
#define eg_beat_on(frame) ((uint8_t)((frame) & (EG_BEAT_LEN - 1u)) < EG_BEAT_ON)

/* Dead-state detection, cheap enough to run after every break or push: a flood fill from `from` through every cell
 * that is not concrete, a pit, a socket or the closed door (everything else might yet be opened: drywall, foam,
 * paper, water, shutters; a pit counts as crossable while a CRT it could be filled with is reachable). Returns 0
 * when the room is still winnable as far as this can tell, else EG_DEAD_DOOR (the door is cut off), EG_DEAD_KEY (a
 * keycard is) or EG_DEAD_ITEM (a piece the room needs is, with no other one in reach or in the pocket). Sound for "dead" (never a false alarm); not complete. */
#define EG_DEAD_DOOR 1u
#define EG_DEAD_KEY 2u
#define EG_DEAD_ITEM 3u
uint8_t eg_dead(const eg_room *r, uint8_t from, const uint8_t *pocket, uint8_t npocket);
/* the same check in slices: begin, then step with a budget (queue pops / key-scan chunks) until it stops returning
 * EG_DEAD_BUSY. One check at a time (static state); the cartridge restarts it whenever the room changes again. */
#define EG_DEAD_BUSY 255u
void eg_dead_begin(uint8_t from);
uint8_t eg_dead_step(const eg_room *r, const uint8_t *pocket, uint8_t npocket, uint8_t budget);

#endif
