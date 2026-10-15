/* EGRESS birth charts (proto): see astro.h and redesign 9.7. */
#include "astro.h"

void astro_pack(const astro_birth *b, uint8_t *st, uint8_t *r) {
  uint32_t m = (uint32_t)b->date * 1440ul + b->mod;
  uint32_t w = (m & 0x03FFFFFFul) | ((uint32_t)(b->quality & 3u) << 26) | ((uint32_t)(b->order & 3u) << 28) |
               ((uint32_t)(b->approx & 1u) << 30);
  st[0] = (uint8_t)w; st[1] = (uint8_t)(w >> 8); st[2] = (uint8_t)(w >> 16); st[3] = (uint8_t)(w >> 24);
  st[4] = (uint8_t)b->utc_q;
  r[0] = (uint8_t)((b->tier[0] & 3u) | ((b->tier[1] & 3u) << 2) | ((b->tier[2] & 3u) << 4) | ((b->tier[3] & 3u) << 6));
  r[1] = (uint8_t)((b->tier[4] & 3u) | ((b->tier[5] & 3u) << 2) | ((b->echo & 7u) << 4));
  r[2] = (uint8_t)((b->asp_pair & 15u) | ((b->asp_tier & 3u) << 4));
}
void astro_unpack(const uint8_t *st, const uint8_t *r, astro_birth *b) {
  uint32_t w = (uint32_t)st[0] | ((uint32_t)st[1] << 8) | ((uint32_t)st[2] << 16) | ((uint32_t)st[3] << 24);
  uint32_t m = w & 0x03FFFFFFul;
  uint8_t i;
  b->date = (uint16_t)(m / 1440ul); b->mod = (uint16_t)(m % 1440ul);
  b->quality = (uint8_t)((w >> 26) & 3u); b->order = (uint8_t)((w >> 28) & 3u); b->approx = (uint8_t)((w >> 30) & 1u);
  b->utc_q = (int8_t)st[4];
  for (i = 0; i < 4u; i++) b->tier[i] = (uint8_t)((r[0] >> (2u * i)) & 3u);
  b->tier[4] = (uint8_t)(r[1] & 3u); b->tier[5] = (uint8_t)((r[1] >> 2) & 3u); b->echo = (uint8_t)((r[1] >> 4) & 7u);
  b->asp_pair = (uint8_t)(r[2] & 15u); b->asp_tier = (uint8_t)((r[2] >> 4) & 3u);
}
uint8_t astro_tiers_used(const astro_birth *b) {
  uint8_t i, n = b->asp_pair < 15u ? b->asp_tier : 0u;
  for (i = 0; i < AP_COUNT; i++) n = (uint8_t)(n + b->tier[i]);
  return n;
}

/* chart points the aspects use, in the order the screen steps through them */
static const uint8_t PT[9] = {SKY_SUN, SKY_MOON, SKY_ASC, SKY_MERCURY, SKY_VENUS, SKY_MARS, SKY_JUPITER, SKY_SATURN, SKY_MC};
static const uint8_t AP_PT[AP_COUNT] = {SKY_SUN, SKY_MOON, SKY_ASC, SKY_MERCURY, SKY_VENUS, SKY_MARS};

void astro_chart_make(uint16_t date, uint16_t mod, astro_chart *c) {
  uint8_t i, j, k;
  uint16_t miss, orb;
  sky_positions(date, mod, c->pos);
  for (i = 0; i < SKY_NPOS; i++) c->sign[i] = (uint8_t)(((uint32_t)c->pos[i] * 12u) >> 16);
  c->nasp = 0;
  for (i = 0; i < 8u; i++)
    for (j = (uint8_t)(i + 1u); j < 8u; j++) {
      orb = (PT[i] == SKY_SUN || PT[i] == SKY_MOON) ? SKY_DEG(8) : SKY_DEG(6); /* the lights get 8 deg, the rest 6 */
      k = sky_aspect(c->pos[PT[i]], c->pos[PT[j]], orb, &miss);
      if (k != 255u && c->nasp < 24u) {
        c->asp[c->nasp].a = PT[i]; c->asp[c->nasp].b = PT[j]; c->asp[c->nasp].kind = k; c->asp[c->nasp].miss = miss;
        c->nasp++;
      }
    }
}

void astro_fx_clear(astro_fx *f) {
  f->aff_elem = f->chg_elem = f->kit_elem = 0; f->opp_elem = 0; f->opp16 = 16; f->aff_mag = f->chg_mag = f->kit_n = 0; f->mood = 0;
  f->price_pct = 0; f->talk_extra = 0;
  f->g_dmg = f->g_def = f->g_avoid = f->g_spd = f->g_status = f->g_hp = 0;
  f->mix_pen = 0; f->foe_pct = 0; f->dark16 = 16; f->hot_pct = 100; f->parry_mag = 0; f->off = 0;
}

/* natal aspect pairs that touch play; the rest are words on the chart screen. Per kind (conj, square, trine, opp):
 * up to two (group, points) nudges. Groups: 0 dmg 1 def 2 avoid 3 spd 4 status 5 hp. */
#define ASTRO_PAIRS 7
static const uint8_t PAIR[ASTRO_PAIRS][2] = {
  {SKY_SUN, SKY_MARS}, {SKY_MARS, SKY_ASC}, {SKY_SUN, SKY_SATURN}, {SKY_SUN, SKY_JUPITER},
  {SKY_MOON, SKY_VENUS}, {SKY_MARS, SKY_SATURN}, {SKY_VENUS, SKY_MARS}};
static const int8_t NUDGE[ASTRO_PAIRS][4][4] = {
  /* Sun-Mars     */ {{0, 2, 9, 0}, {0, 3, 2, -2}, {0, 2, 9, 0}, {1, 2, 0, -1}},
  /* Mars-Rising  */ {{2, 2, 9, 0}, {0, 2, 1, -2}, {2, 1, 9, 0}, {1, 1, 2, -1}},
  /* Sun-Saturn   */ {{1, 2, 3, -1}, {1, 2, 2, -2}, {1, 2, 9, 0}, {1, 1, 0, -1}},
  /* Sun-Jupiter  */ {{5, 4, 9, 0}, {5, 4, 1, -1}, {5, 4, 9, 0}, {5, 2, 9, 0}},
  /* Moon-Venus   */ {{5, 3, 9, 0}, {9, 0, 9, 0}, {5, 3, 9, 0}, {9, 0, 9, 0}},
  /* Mars-Saturn  */ {{1, 2, 2, -1}, {1, 2, 0, -2}, {1, 1, 9, 0}, {1, 1, 0, -1}},
  /* Venus-Mars   */ {{4, 2, 9, 0}, {4, 2, 1, -1}, {4, 2, 9, 0}, {4, 1, 9, 0}}};

static void nudge_(astro_fx *f, uint8_t g, int8_t v) {
  switch (g) {
  case 0: f->g_dmg = (int8_t)(f->g_dmg + v); break;
  case 1: f->g_def = (int8_t)(f->g_def + v); break;
  case 2: f->g_avoid = (int8_t)(f->g_avoid + v); break;
  case 3: f->g_spd = (int8_t)(f->g_spd + v); break;
  case 4: f->g_status = (int8_t)(f->g_status + v); break;
  case 5: f->g_hp = (int8_t)(f->g_hp + v); break;
  default: break;
  }
}
static int8_t clamp4_(int8_t v) { return v > 4 ? 4 : v < -4 ? -4 : v; }

void astro_natal_fx(const astro_chart *c, const astro_birth *b, astro_fx *f) {
  uint8_t i, p, t[AP_COUNT];
  for (i = 0; i < AP_COUNT; i++) t[i] = b->tier[i];
  /* the parent's echo: half a tier, never past the tier-2 ceiling (it shows as a faint ring on the wheel) */
  f->aff_elem = astro_elem(c->sign[SKY_SUN]);
  f->aff_mag = (uint8_t)(3u + 2u * t[AP_SUN] + (b->echo == AP_SUN && t[AP_SUN] < 2u ? 1u : 0u));
  f->opp_elem = (uint8_t)(f->aff_elem ^ 3u); /* fire<->water, earth<->air */
  f->opp16 = 17;                             /* +6%: every placement is an upside with a cost */
  f->chg_elem = astro_elem(c->sign[SKY_MARS]);
  f->chg_mag = (uint8_t)(2u + 2u * t[AP_MARS] + (b->echo == AP_MARS && t[AP_MARS] < 2u ? 1u : 0u));
  f->kit_elem = astro_elem(c->sign[SKY_ASC]);
  f->kit_n = (uint8_t)(1u + t[AP_RISING]);
  f->mood = (uint8_t)(astro_elem(c->sign[SKY_MOON]) | (t[AP_MOON] << 2));
  f->price_pct = (int8_t)-(3 + 2 * (int8_t)t[AP_VENUS]);
  f->talk_extra = (uint8_t)(6u - 2u * t[AP_MERCURY]); /* 1 talk in 6, 4, 2 */
  for (i = 0; i < c->nasp; i++)
    for (p = 0; p < ASTRO_PAIRS; p++) {
      const astro_asp *a = &c->asp[i];
      if ((a->a == PAIR[p][0] && a->b == PAIR[p][1]) || (a->a == PAIR[p][1] && a->b == PAIR[p][0])) {
        const int8_t *n = NUDGE[p][a->kind];
        int8_t m = (int8_t)(b->asp_pair == p ? 2 + (int8_t)b->asp_tier : 2); /* x1, x1.5, x2 (halves) */
        if (n[0] < 9) nudge_(f, (uint8_t)n[0], (int8_t)((n[1] * m) / 2));
        if (n[2] < 9) nudge_(f, (uint8_t)n[2], n[3]); /* the cost never grows */
      }
    }
  /* a chart can only lean so far: four points per group */
  f->g_dmg = clamp4_(f->g_dmg); f->g_def = clamp4_(f->g_def); f->g_avoid = clamp4_(f->g_avoid);
  f->g_spd = clamp4_(f->g_spd); f->g_status = clamp4_(f->g_status); f->g_hp = clamp4_(f->g_hp);
}

static const uint8_t NATAL6[6] = {SKY_SUN, SKY_MOON, SKY_MERCURY, SKY_VENUS, SKY_MARS, SKY_ASC};
uint8_t astro_transits(const astro_chart *n, const uint16_t *now, astro_hit *out, uint8_t max) {
  uint8_t b, j, k, cnt = 0;
  uint16_t miss, orb;
  for (b = SKY_SUN; b < SKY_NPLANET; b++) {
    orb = b == SKY_MOON ? SKY_DEG(4) : b >= SKY_JUPITER ? (uint16_t)(SKY_DEG(3) / 2u) : SKY_DEG(2);
    for (j = 0; j < 6u; j++) {
      k = sky_aspect(now[b], n->pos[NATAL6[j]], orb, &miss);
      if (k == 255u || cnt >= max) continue;
      out[cnt].body = b; out[cnt].natal = NATAL6[j]; out[cnt].kind = k; out[cnt].miss = miss;
      cnt++;
    }
  }
  return cnt;
}

uint16_t astro_states(const sky_now *n, const astro_chart *natal) {
  uint16_t s = 0, miss;
  if (n->retro & (1u << SKY_MERCURY)) s |= AS_MERC_RX;
  if (n->retro & (1u << SKY_VENUS)) s |= AS_VENUS_RX;
  if (n->retro & (1u << SKY_MARS)) s |= AS_MARS_RX;
  if (n->station & (1u << SKY_JUPITER)) s |= AS_JUP_STATION;
  if (n->station & (1u << SKY_SATURN)) s |= AS_SAT_STATION;
  if (n->flags & SKY_ECLIPSE) s |= n->eclipse >= 5u ? AS_ECL_MOON : AS_ECL_SUN;
  if (n->phase8 == 4u) s |= AS_FULL;
  if (n->phase8 == 0u) s |= AS_NEW;
  if ((n->flags & SKY_SUPERM) && (n->phase8 == 0u || n->phase8 == 4u)) s |= AS_SUPER;
  if (n->flags & SKY_VOC) s |= AS_VOC;
  if (((uint16_t)(n->pos[SKY_SUN] + 0x0400u) & 0x3FFFu) < 0x0800u) s |= AS_SEASON; /* within ~5.6 deg of 0/90/180/270 */
  if (natal) {
    if (sky_aspect(n->pos[SKY_SUN], natal->pos[SKY_SUN], SKY_DEG(1), &miss) == SKY_CONJ) s |= AS_SOLAR_RETURN;
    if (sky_aspect(n->pos[SKY_MOON], natal->pos[SKY_MOON], SKY_DEG(6), &miss) == SKY_CONJ) s |= AS_LUNAR_RETURN;
  }
  return s;
}

/* a transit's nudge: (group, points, cost group, cost) by transiting body and aspect family (soft = conj/trine) */
static void hit_fx_(const astro_hit *h, astro_fx *f) {
  uint8_t soft = h->kind == SKY_CONJ || h->kind == SKY_TRINE;
  switch (h->body) {
  case SKY_SUN: if (soft) nudge_(f, 4, 1); break;
  case SKY_MOON: if (h->natal == SKY_SUN && h->kind == SKY_CONJ) nudge_(f, 4, 2); break;
  case SKY_MERCURY: break; /* talk flavour only */
  case SKY_VENUS: if (soft) { nudge_(f, 4, 1); f->price_pct = (int8_t)(f->price_pct - 3); } break;
  case SKY_MARS: if (soft) nudge_(f, 0, 2); else { nudge_(f, 0, 2); nudge_(f, 2, -2); } break;
  case SKY_JUPITER: if (soft) nudge_(f, 5, 4); else { nudge_(f, 5, 3); nudge_(f, 1, -1); } break;
  case SKY_SATURN: if (soft) nudge_(f, 1, 2); else { nudge_(f, 1, 2); nudge_(f, 3, -2); } break;
  default: break;
  }
}

void astro_session_fx(uint16_t s, const astro_hit *hits, uint8_t nh, astro_fx *f) {
  uint8_t i, j, used[2] = {255, 255};
  uint16_t best;
  /* the two tightest combat-facing transits */
  for (j = 0; j < 2u; j++) {
    best = 0xFFFFu;
    for (i = 0; i < nh; i++)
      if (i != used[0] && hits[i].body != SKY_MERCURY && hits[i].miss < best) { best = hits[i].miss; used[j] = i; }
    if (used[j] != 255u) hit_fx_(&hits[used[j]], f);
  }
  if (s & AS_SOLAR_RETURN) nudge_(f, 5, 6); /* a candle: a bit of a heart for the run */
  if (s & AS_LUNAR_RETURN) nudge_(f, 4, 2);
  /* the sky today (redesign 9.7: real but bounded) */
  if (s & (AS_MERC_RX | AS_VOC)) f->mix_pen = 1;            /* a recipe misfires: one more step in mix rooms */
  if (s & AS_MARS_RX) { f->hot_pct = 75; f->parry_mag = 4; } /* fire hits softer, parries hold better */
  if (s & AS_JUP_STATION) f->off = (int8_t)(f->off + 3);     /* more treasure: better pickups */
  if (s & AS_SAT_STATION) f->foe_pct = (uint8_t)(f->foe_pct + 4); /* tougher elites */
  if (s & AS_ECL_SUN) { f->dark16 = 20; f->foe_pct = (uint8_t)(f->foe_pct + 2); }
  if (s & AS_ECL_MOON) f->foe_pct = (uint8_t)(f->foe_pct + 2); /* more ghosts */
  if (s & AS_FULL) { f->foe_pct = (uint8_t)(f->foe_pct + ((s & AS_SUPER) ? 5u : 3u)); f->off = (int8_t)(f->off + ((s & AS_SUPER) ? 3 : 2)); }
  if (s & AS_NEW) { if (f->dark16 < 18u) f->dark16 = (s & AS_SUPER) ? 19u : 18u; f->off = (int8_t)(f->off + 1); }
  /* however the sky stacks, it leans one way only so far */
  if (f->foe_pct > 5u) f->foe_pct = 5u;
  if (f->off > 3) f->off = 3;
}

uint8_t astro_can_align(const astro_birth *b, const astro_chart *natal, uint8_t p, const sky_now *now) {
  uint16_t miss;
  uint8_t body;
  if (b->tier[p] >= 2u) return 4;
  if (astro_tiers_used(b) >= ASTRO_TIER_CAP) return 1;
  if (now->flags & SKY_VOC) return 3;
  if (b->tier[p] == 1u) {
    /* tier 2 needs the sky: the Moon, or the planet itself, over the natal placement or trine it */
    for (body = SKY_SUN; body < SKY_NPLANET; body++) {
      uint8_t k;
      if (body != SKY_MOON && body != AP_PT[p]) continue;
      k = sky_aspect(now->pos[body], natal->pos[AP_PT[p]], SKY_DEG(3), &miss);
      if (k == SKY_CONJ || k == SKY_TRINE) return 0;
    }
    return 2;
  }
  return 0;
}

uint8_t astro_echo_of(const astro_birth *parent) {
  uint8_t i, best = 7, bt = 0;
  for (i = 0; i < AP_COUNT; i++)
    if (parent->tier[i] > bt) { bt = parent->tier[i]; best = i; }
  return best;
}
