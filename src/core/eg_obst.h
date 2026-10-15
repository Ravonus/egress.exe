#ifndef EG_OBST_H
#define EG_OBST_H
/* EGRESS living obstacles (docs/redesign.md 7.7, B2): mobs that can't die, part of the puzzle machinery. Every rule
 * is deterministic and lives here, so the cartridge (src/obstacle.c) and the host tick solver (host/eg_osolve.c) run
 * the same code.
 *
 * The room tick (every EG_OB_TICK frames on the cartridge; one solver step on the host) runs, in order:
 *   1 latch     the caller fills eg_otick: the player's cell P, the previous latched cell P', the 8-way facing, the
 *               tick count, and the noise the player made since the last tick (glass breaking under them)
 *   2 statuses  stop counters (stun, freeze, root), the immunity after a stop, the sleeper's yawn drop by 1
 *   3 sense     all obstacles at once against the latched state: the sentry's cone and line of sight, lures (noise in
 *               the row or column, a clear line, within 5), noise waking sleepers, the follower's wake, the mirror's
 *               delta P - P'
 *   4 act       slot order, each on its phase: target cell, resolve (the collision table), move, push, eat
 *   5 tiles     step-off cracks (the heavy weighs 2) and partition breaks happen as each obstacle moves (inline)
 * The caller then runs the shutter pass (eg_gravity) and, when EV_TILES is set, the dead check.
 *
 * Collision (shared): walls, the door, pits (the patroller rides over pits), closed shutters, crates (the pusher and
 * the mirror push them), other obstacles and glass partitions (the heavy breaks in) block. The player is SHOVED one
 * cell along the obstacle's path onto plain floor; if that cell can't take them they are PINNED and TRAMPLED (1 hit,
 * the two swap cells). The sentry CATCHES instead (1 hit, back to its plinth). A body never blocks an obstacle.
 *
 * Portable C for SDCC (SM83) and host compilers: 8-bit arithmetic, no recursion, no allocation. */
#include <stdint.h>
#include "eg_room.h"

enum { OB_PATROL = 0, /* COMMUTER: walks its route (the room's run table), reverses when blocked; rides over pits */
       OB_HEAVY,      /* MOVER: straight ahead every 2nd tick, turns when blocked; weighs 2; breaks partitions */
       OB_PUSHER,     /* FILE CART: straight ahead, pushes the crate it faces */
       OB_EATER,      /* SHREDDER: straight ahead, eats tokens and pieces it walks over (never keycards) */
       OB_MIRROR,     /* OTHER TEMP: copies the player's step, flipped on its axis (variant 1 x, 2 y, 3 both) */
       OB_SENTRY,     /* COAT RACK: one cell toward the player on every tick it is not looked at */
       OB_LIGHT,      /* POWER CUT: not built in B2 (the record is reserved) */
       OB_DRIFTER,    /* REORG: not built in B2 */
       OB_FOLLOWER,   /* REPLY ALL: dormant until the player passes next to it, then always one step behind */
       OB_SLEEPER,    /* STANDBY: asleep (solid) until noise within its radius; a 2-tick yawn; then variant 0 walks the
                         route, variant 1 is a heavy */
       OB_RULES };

/* the baked record (4 bytes, eg_room.ob[k]) */
#define OBA_RULE(a) ((uint8_t)((a) & 15u))
#define OBA_P2 0x10u   /* period 2 (acts on ticks whose parity is OBP_PHASE) */
#define OBA_LEFT 0x20u /* turns left when blocked (else right) */
#define OBA_DARK 0x40u /* the DARK body (look only) */
#define OBA_LOCK 0x80u /* the lock glyph (anchored: the sentry, a sleeper asleep) */
#define OBB_DIR(b) ((uint8_t)((b) & 3u)) /* 0 N, 1 E, 2 S, 3 W: turning right is +1 */
#define OBB_VAR(b) ((uint8_t)(((b) >> 2) & 7u))
#define OBB_ASLEEP 0x80u
#define OBP_ARG(p) ((uint8_t)((p) & 0x7fu)) /* the route position (patroller, sleeper 0), the wake radius (sleeper 1) */
#define OBP_PHASE 0x80u

/* runtime, 8 bytes per obstacle */
typedef struct {
  uint8_t cell;  /* where it stands (the cell carries F_OB) */
  uint8_t from;  /* where it stood before its last move (the cartridge's tween) */
  uint8_t dir;   /* 0-3, OBD_REV: walking its route backwards */
  uint8_t pos;   /* route position (steps from the route's start) */
  uint8_t stop;  /* OBS_* kind << 4 | ticks left */
  uint8_t imm;   /* ticks it ignores stops for */
  uint8_t st;    /* OBF_* */
  uint8_t belly; /* the eater: things eaten; the sentry: its pose */
} eg_ob;
#define OBD_REV 4u
#define OBF_AWAKE 0x01u /* the sleeper and the follower: awake */
#define OBF_YAWN 0x06u  /* the sleeper's yawn ticks left (<< 1) */
#define OBF_MOVED 0x08u /* moved on the last tick */
#define OBF_TAGGED 0x80u /* the cartridge: its name tag has been shown */
enum { OBS_NONE = 0, OBS_STUN, OBS_FREEZE, OBS_ROOT };
#define EG_OB_IMMUNE 4u /* ticks a stopped obstacle ignores stops for after the stop ends */
#define EG_OB_LURE 5u   /* lure reach in cells (eaters: EG_OB_FOOD) */
#define EG_OB_FOOD 4u
#define EG_OB_WAKE 4u   /* the default wake radius */

typedef struct {
  uint8_t p, pp, face, tick; /* latched: the player's cell, the previous latched cell, EG_FACE_* (8-way), tick count */
  uint8_t nnoise, noise[2];  /* noise the player made since the last tick */
  uint8_t hits;              /* out: tramples and catches (1 hit each) */
  uint8_t ev;                /* out: EV_* */
  uint8_t nchg, chg[6];      /* out: cells whose type changed (cracks, breaks, pushes, eaten things) */
} eg_otick;
#define EV_MOVED 0x01u
#define EV_BROKE 0x02u  /* a partition broke */
#define EV_TILES 0x04u  /* a tile, crate or item changed: run the dead check */
#define EV_SHOVE 0x08u
#define EV_HIT 0x10u
#define EV_WOKE 0x20u
#define EV_PUSH 0x40u
#define EV_CATCH 0x80u

/* the obstacles' runtime from the room's records; marks their cells F_OB */
void eg_ob_init(eg_room *r, eg_ob *ob) EG_BANKED;
/* one room tick (steps 2-5 above). t->p comes back moved if the player was shoved or trampled. */
void eg_ob_tick(eg_room *r, eg_ob *ob, eg_otick *t) EG_BANKED;
/* a status from the reaction table (stun, freeze, root) for `ticks`; refused (0) while stopped or immune */
uint8_t eg_ob_stop(eg_ob *o, uint8_t kind, uint8_t ticks) EG_BANKED;
/* GLITCH: reversed once (facing +2, route direction flipped, mirror axis swapped) */
void eg_ob_reverse(const eg_room *r, eg_ob *o, uint8_t k) EG_BANKED;
/* the sentry's sight test: the player at p facing `face` (EG_FACE_*) sees the cell s (90-degree cone, Bresenham line
 * of sight through cells that don't block sight) */
uint8_t eg_ob_seen(const eg_room *r, uint8_t p, uint8_t face, uint8_t s) EG_BANKED;
/* the obstacle (slot) standing at `cell`, or 255 */
uint8_t eg_ob_at(const eg_room *r, const eg_ob *ob, uint8_t cell);
/* the route's total length in steps */
uint8_t eg_route_len(const eg_room *r);
/* difficulty cost per rule (7.7): patroller 2, wanderer 3, pusher 3, eater 2, mirror 4, sentry 3, light 2,
 * drifter 4, follower 2, sleeper 2 */
extern const uint8_t eg_ob_cost[OB_RULES];
/* the cell an obstacle may walk into (not counting the player, crates, partitions): carpet, plates, open shutters,
 * planks, keycards, tokens, pieces and fragile floor; pits too for a rider (the patroller) */
uint8_t eg_ob_walk(uint8_t v, uint8_t rider);

#endif
