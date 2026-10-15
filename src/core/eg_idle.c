/* EGRESS heir idle scheduler (see eg_idle.h and proto/anim/heirs.py idle_plan). */
#include "eg_idle.h"

/* heirs.py state lengths (sum of frame durations at 60 Hz) */
const uint8_t eg_idle_len[EG_ID_COUNT] = {
    0u,                                              /* BASE: scheduled */
    82u, 44u, 50u, 44u, 34u, 50u, 40u, 50u, 17u, 15u, 22u, /* fidgets */
    0u, 12u, 0u, 0u,                                 /* long sit (scheduled), sit twitch, doze, lie */
    0u, 0u, 0u,                                      /* ready 180, cool 120, low HP: scheduled */
};

enum { PH_READY = 0, PH_COOL, PH_BASE, PH_FIDGET, PH_LONG_SIT, PH_SIT, PH_SIT_TWITCH, PH_FOREVER };

static uint8_t u8(eg_idle_t *s) {
  s->s = s->s * 1103515245ul + 12345ul;
  return (uint8_t)(s->s >> 16);
}
static uint8_t below(eg_idle_t *s, uint8_t n) { return (uint8_t)(((uint16_t)u8(s) * n) >> 8); }

static uint8_t weight(const eg_idle_t *s, uint8_t f) {
  uint8_t w = 4u, tr = s->traits;
  if (f == EG_ID_F_GLITCH) w = (tr & EG_IT_GLITCHED) ? 14u : 1u;
  if (f == EG_ID_F_YAWN && (tr & EG_IT_INSOMNIAC)) w = 0u;
  if (f == EG_ID_F_SHIFT && (tr & EG_IT_HEAVY)) w = 16u;
  if (f == EG_ID_F_TWITCH) w = (tr & EG_IT_INSOMNIAC) ? 10u : 0u;
  if (f == EG_ID_F_BOUNCE) w = (tr & EG_IT_SMALL) ? 10u : 0u;
  if (f == s->last) w = 0u;
  return w;
}

static void seg(eg_idle_t *s, uint8_t id, uint16_t dur, uint8_t ph) {
  s->seg = id;
  s->left = dur;
  s->phase = ph;
}

/* heirs.py's loop top: the long idle after 10 s, else a stretch of the base loop */
static void top(eg_idle_t *s) {
  if (s->t >= EG_IDLE_LONG_AFTER) {
    if (s->ctx & EG_IC_LOWHP) seg(s, EG_ID_LOWHP, EG_IDLE_FOREVER, PH_FOREVER);
    else seg(s, EG_ID_LONG_SIT, 240u, PH_LONG_SIT);
    return;
  }
  seg(s, (s->ctx & EG_IC_LOWHP) ? EG_ID_LOWHP : EG_ID_BASE, (uint16_t)(EG_IDLE_GAP0 + below(s, EG_IDLE_GAP1)),
      PH_BASE);
}

static void pick_fidget(eg_idle_t *s) {
  uint8_t f, tot = 0u, r;
  for (f = EG_ID_F_LOOK; f <= EG_ID_F_BOUNCE; f++) tot = (uint8_t)(tot + weight(s, f));
  r = below(s, tot);
  for (f = EG_ID_F_LOOK; f < EG_ID_F_BOUNCE; f++) {
    uint8_t w = weight(s, f);
    if (!w) continue;
    if (r < w) break;
    r = (uint8_t)(r - w);
  }
  while (!weight(s, f)) f--; /* only when r runs past the end, which the weights rule out */
  s->last = f;
  seg(s, f, eg_idle_len[f], PH_FIDGET);
}

static void next(eg_idle_t *s) {
  switch (s->phase) {
    case PH_READY:
      seg(s, EG_ID_COOL, 120u, PH_COOL);
      return;
    case PH_BASE:
      if (s->t >= EG_IDLE_LONG_AFTER) top(s);
      else pick_fidget(s);
      return;
    case PH_LONG_SIT:
      if (s->traits & EG_IT_INSOMNIAC) seg(s, EG_ID_LONG_SIT, (uint16_t)(120u + below(s, 120u)), PH_SIT);
      else if (below(s, 2u) == 0u) seg(s, EG_ID_LONG_DOZE, EG_IDLE_FOREVER, PH_FOREVER);
      else seg(s, EG_ID_LONG_LIE, EG_IDLE_FOREVER, PH_FOREVER);
      return;
    case PH_SIT:
      seg(s, EG_ID_LONG_SIT_TWITCH, eg_idle_len[EG_ID_LONG_SIT_TWITCH], PH_SIT_TWITCH);
      return;
    case PH_SIT_TWITCH:
      seg(s, EG_ID_LONG_SIT, (uint16_t)(120u + below(s, 120u)), PH_SIT);
      return;
    case PH_FOREVER:
      s->left = EG_IDLE_FOREVER;
      return;
    default: /* PH_COOL, PH_FIDGET */
      top(s);
      return;
  }
}

void eg_idle_init(eg_idle_t *s, uint16_t seed, uint8_t traits, uint8_t ctx) {
  s->s = (uint32_t)seed * 2654435761ul + 1ul;
  if (!s->s) s->s = 1ul;
  s->t = 0u;
  s->last = 0xffu;
  s->traits = traits;
  s->ctx = ctx;
  if ((ctx & EG_IC_FIGHT) && !(ctx & EG_IC_LOWHP)) {
    seg(s, EG_ID_READY, 180u, PH_READY);
    s->ctx &= (uint8_t)~EG_IC_FIGHT;
  } else
    top(s);
}

uint8_t eg_idle_step(eg_idle_t *s) {
  if (s->t != 0xffffu) s->t++;
  if (s->left == EG_IDLE_FOREVER) return 0u;
  if (--s->left) return 0u;
  next(s);
  return 1u;
}
