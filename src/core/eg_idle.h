#ifndef EG_IDLE_H
#define EG_IDLE_H
/* EGRESS heir idle scheduler: proto/anim/heirs.py idle_plan() as a per-tick state machine.
 *
 * A base loop (breathing; weapon-ready then cooling down after a fight; hunched when HP is low) with a seeded fidget
 * every 2.5-6 s, never the same fidget twice in a row; after 10 s a long idle (sit, then doze or lie down; an
 * INSOMNIAC never dozes and keeps sitting with twitches; low HP never lies down). Any input cancels it: the caller
 * re-inits on the next idle. Deterministic from the seed: host/test_idle.c checks it against heirs.py tick for tick.
 *
 * Portable C for SDCC (SM83) and host compilers: no floats, no allocation, no globals but const tables. */
#include <stdint.h>

/* segments (what the caller animates) */
enum {
  EG_ID_BASE = 0, /* the base loop: breathe (or EG_ID_LOWHP when HP is low) */
  EG_ID_F_LOOK, EG_ID_F_STRETCH, EG_ID_F_LANYARD, EG_ID_F_YAWN, EG_ID_F_SCRATCH, EG_ID_F_SHIFT, EG_ID_F_TAP,
  EG_ID_F_GLANCE, EG_ID_F_GLITCH, EG_ID_F_TWITCH, EG_ID_F_BOUNCE, /* the fidget pool, heirs.py order */
  EG_ID_LONG_SIT, EG_ID_LONG_SIT_TWITCH, EG_ID_LONG_DOZE, EG_ID_LONG_LIE,
  EG_ID_READY, EG_ID_COOL, EG_ID_LOWHP,
  EG_ID_COUNT
};
#define EG_ID_FIDGETS 11u
#define EG_IDLE_LONG_AFTER 600u
#define EG_IDLE_GAP0 150u
#define EG_IDLE_GAP1 210u
#define EG_IDLE_FOREVER 0xffffu

/* traits that change the pool */
#define EG_IT_INSOMNIAC 0x01u
#define EG_IT_HEAVY 0x02u
#define EG_IT_GLITCHED 0x04u
#define EG_IT_SMALL 0x08u
/* contexts */
#define EG_IC_FIGHT 0x01u
#define EG_IC_LOWHP 0x02u

extern const uint8_t eg_idle_len[EG_ID_COUNT]; /* fixed segment lengths (fidgets, the sit twitch); 0 = scheduled */

typedef struct {
  uint32_t s;       /* the LCG (heirs.py _Lcg) */
  uint16_t t;       /* ticks since the idle began */
  uint16_t left;    /* ticks left in the segment (EG_IDLE_FOREVER: until input) */
  uint8_t seg;      /* EG_ID_* */
  uint8_t last;     /* last fidget, 0xff none */
  uint8_t phase;    /* internal */
  uint8_t traits, ctx;
} eg_idle_t;

void eg_idle_init(eg_idle_t *s, uint16_t seed, uint8_t traits, uint8_t ctx);
/* Advance one tick. Returns 1 when a new segment starts on this tick (the caller restarts its animation). */
uint8_t eg_idle_step(eg_idle_t *s);

#endif
