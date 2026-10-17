/* B2 particles (docs/redesign.md 5.5): a pool of 8, the oldest recycled first, drawn last (OP_FX: refused before
 * anything else on a full line, never flickered for). A burst per hit, kill, reaction and tag, in the tag's shape
 * and colour: shards for COLD, drops for WET, embers for HOT, bolts for a short, leaves for ALIVE, runes for MAGIC. */
#pragma bank 255
#include <gb/gb.h>
#include "play.h"
#include "mob.h"
#include "oam.h"
#include "gen/mob_art.h"
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;

#define FXN 8u
typedef struct {
  uint8_t t, tile, pal, x, y;
  int8_t vx, vy;
} fx_t;
static fx_t fx[FXN];
static uint8_t fx_next;

/* per FX_*: shape, palette (4 the lead's, 5 the floor's FX colours, 7 the elite's), life, speed */
static const uint8_t fx_shape[FX_COUNT] = {FXT_STAR, FXT_DOT,  FXT_EMBER, FXT_SHARD, FXT_DROP, FXT_BOLT,
                                           FXT_STAR, FXT_LEAF, FXT_RUNE,  FXT_SHARD, FXT_DOT,  FXT_CROWN};
static const uint8_t fx_pal[FX_COUNT] = {4u, 5u, 5u, 4u, 4u, 4u, 4u, 5u, 5u, 5u, 5u, 7u};
static const uint8_t fx_life[FX_COUNT] = {8u, 18u, 22u, 18u, 20u, 12u, 12u, 22u, 22u, 14u, 16u, 24u};
static const int8_t fx_vx[4] = {-12, 12, -6, 6}, fx_vy[4] = {-16, -16, -24, -10};

void fx_reset(void) BANKED {
  uint8_t i;
  for (i = 0; i != FXN; i++) fx[i].t = 0;
  fx_next = 0;
}

void fx_burst(uint8_t x, uint8_t y, uint8_t kind, uint8_t n) BANKED {
  fx_t *p;
  uint8_t life,tile,pal,v;
  if (kind >= FX_COUNT) kind = FX_HIT;
  life=fx_life[kind];tile=(uint8_t)(MOB_FX_TILE+fx_shape[kind]*2u);pal=fx_pal[kind];
  while (n--) {
    p = &fx[fx_next];
    fx_next = (uint8_t)((fx_next + 1u) & (FXN - 1u));
    p->t = life;
    p->tile = tile;
    p->pal = pal;
    p->x = x;
    p->y = y;
    if (kind == FX_HIT) {
      p->vx = p->vy = 0;
    } else {
      v=(uint8_t)(n+fx_next)&3u;
      p->vx = fx_vx[v];
      p->vy = fx_vy[v];
      if (kind == FX_WET || kind == FX_KILL) p->vy = (int8_t)(-p->vy >> 1);
    }
  }
}

/* x, y are pixels; velocities in 1/16 px, applied every frame (positions keep a 1/16 fraction in the low bits
 * of a frame counter: cheap and good enough for sparks) */
void fx_draw(void) BANKED {
  if(!(fx[0].t|fx[1].t|fx[2].t|fx[3].t|fx[4].t|fx[5].t|fx[6].t|fx[7].t))return;
  fx_t *p = fx;
  uint8_t i,quota=(uint8_t)(LY_REG>=88u ? 0u : FXN);
  for (i = FXN; i; i--, p++) {
    if (!p->t) continue;
    p->t--;
    if (p->t & 1u) {
      p->x = (uint8_t)(p->x + (p->vx >> 3));
      p->y = (uint8_t)(p->y + (p->vy >> 3));
      p->vy = (int8_t)(p->vy + 2);
    }
    if(quota && video_epoch==frame_epoch && !(LY_REG>=88u && LY_REG<144u)) {
      oam_group((int16_t)p->x - 8 - cam_x, (int16_t)p->y - 8 - cam_y, 1u, p->tile, 0, p->pal);
      quota--;
    }
  }
}
