/* EGRESS sky engine (proto): see sky.h. Integer only; no floats, no division in the hot path except atan2's one. */
#include "sky.h"

#define COSE 15033 /* cos 23.4393 deg, Q14 */
#define SINE 6517
#define TPSE 5469 /* tan 40 deg * sin eps, Q14: the shared world's latitude, 40 N */
#define ABERR 4u  /* the Sun's annual aberration, -20.5", in binary angle */

static int16_t sin13_(uint16_t i) { /* i: 8192 steps a turn */
  uint16_t j = i & 2047u;
  uint8_t q = (uint8_t)(i >> 8) & 0x18u; /* bits 11, 12 */
  int16_t v;
  if (q & 0x08u) j = (uint16_t)(2048u - j);
  v = sky_sinq[j];
  return (q & 0x10u) ? (int16_t)-v : v;
}
int16_t sky_sinf(uint16_t a) { return sin13_((uint16_t)((uint16_t)(a + 4u) >> 3)); }
int16_t sky_sin(uint16_t a) { /* linear between the table steps, so slow arguments move smoothly */
  uint16_t i = (uint16_t)(a >> 3);
  int16_t s0 = sin13_(i), f = (int16_t)(a & 7u);
  if (!f) return s0;
  {
    int16_t d = (int16_t)(sin13_((uint16_t)(i + 1u)) - s0), p = 0; /* |d| <= 13: d * f / 8 by shifts */
    if (f & 4) p += (int16_t)(d << 2);
    if (f & 2) p += (int16_t)(d << 1);
    if (f & 1) p += d;
    return (int16_t)(s0 + (p >> 3));
  }
}

static uint16_t atan_oct_(uint16_t lo, uint16_t hi) { /* atan(lo / hi), lo <= hi: 1/16384 ratio steps, interpolated */
  uint16_t q = (uint16_t)(((uint32_t)lo << 14) / hi), i = (uint16_t)(q >> 4), a = sky_atan[i];
  if (q & 15u) a = (uint16_t)(a + (uint16_t)(((uint16_t)(sky_atan[i + 1u] - a) * (q & 15u)) >> 4));
  return a;
}
uint16_t sky_atan2(int16_t y, int16_t x) {
  uint16_t ax = (uint16_t)(x < 0 ? -x : x), ay = (uint16_t)(y < 0 ? -y : y), t;
  if (!ax && !ay) return 0;
  if (ay <= ax) t = atan_oct_(ay, ax);
  else t = (uint16_t)(0x4000u - atan_oct_(ax, ay));
  if (x < 0) t = (uint16_t)(0x8000u - t);
  if (y < 0) t = (uint16_t)(0u - t);
  return t;
}

static uint16_t ang_[SKY_NANG];

static uint16_t kmul_(int8_t k, uint16_t a) { /* k * a for the small multipliers (|k| <= 6) by additions */
  uint16_t r = 0;
  if (k < 0) { k = (int8_t)-k; a = (uint16_t)(0u - a); }
  while (k) { r = (uint16_t)(r + a); k--; }
  return r;
}
static int32_t series_(uint8_t t0, uint8_t n) {
  int32_t acc = 0;
  const sky_term *t = sky_terms + t0;
  uint16_t a;
  for (; n; n--, t++) {
    a = (uint16_t)(t->ph + kmul_(t->k1, ang_[t->a1]));
    if (t->k2) a = (uint16_t)(a + kmul_(t->k2, ang_[t->a2]));
    if (t->k3) a = (uint16_t)(a + kmul_(t->k3, ang_[t->a3]));
    acc += sky_mul16(t->amp, sky_sinf(a));
  }
  return acc;
}

static uint16_t lon_(uint8_t b) {
  const sky_body *d = &sky_bodies[b];
  return (uint16_t)(ang_[d->out] + (uint16_t)((series_(d->t0, d->nl) + 32768) >> 16));
}
static int16_t rad11_(uint8_t b) { /* radius, Q11 AU */
  const sky_body *d = &sky_bodies[b];
  return (int16_t)(d->r0 + (int16_t)((series_(d->tr, d->nr) + 32768) >> 16));
}
static int16_t mulq14_(int16_t a, int16_t b) { /* (a * b) >> 14 by byte moves */
  int32_t p = sky_mul16(a, b);
  return (int16_t)(((uint16_t)(p >> 16) << 2) | ((uint8_t)((uint16_t)p >> 8) >> 6));
}
static uint16_t cdate_ = 0xFFFFu;
static uint32_t aday_[SKY_NANG]; /* a0 + rate_day * date, for the last date asked */
static uint16_t mean_(uint8_t i, uint16_t date, uint16_t mod) { /* top 16 bits of a0 + rate_day * date + rate_min * mod */
  uint32_t q = sky_angm[i];
  if (date != cdate_) { /* sky_positions refills the whole cache when the date changes */
    uint32_t r = sky_angd[i];
    aday_[i] = sky_ang0[i] + sky_mulu16((uint16_t)r, date) + ((uint32_t)(uint16_t)((uint16_t)(r >> 16) * date) << 16);
  }
  return (uint16_t)((aday_[i] + sky_mulu16((uint16_t)q, mod) + ((uint32_t)(uint16_t)((uint16_t)(q >> 16) * mod) << 16)) >> 16);
}

void sky_positions(uint16_t date, uint16_t mod, uint16_t *out) {
  uint8_t i;
  uint16_t le, lp, th;
  int16_t re, rp, ex, ey, s, c;
  for (i = 0; i < SKY_NANG; i++)
    ang_[i] = mean_(i, date, mod);
  cdate_ = date;
  le = lon_(0);
  re = rad11_(0);
  ex = mulq14_(re, sky_cos(le));
  ey = mulq14_(re, sky_sin(le));
  out[SKY_SUN] = (uint16_t)(le + 0x8000u - ABERR);
  out[SKY_MOON] = lon_(1);
  for (i = 2; i < SKY_NPLANET; i++) {
    lp = lon_(i);
    rp = rad11_(i);
    out[i] = sky_atan2((int16_t)(mulq14_(rp, sky_sin(lp)) - ey), (int16_t)(mulq14_(rp, sky_cos(lp)) - ex));
  }
  out[SKY_NODE] = ang_[SKY_ANG_NODE];
  th = ang_[SKY_ANG_GMST]; /* the building stands on the shared world's meridian 0 */
  s = sky_sin(th);
  c = sky_cos(th);
  out[SKY_ASC] = sky_atan2(c, (int16_t)(-mulq14_(s, COSE) - TPSE));
  out[SKY_MC] = sky_atan2(s, mulq14_(c, COSE));
}

int32_t sky_minutes(uint16_t date, uint16_t mod) { return ((int32_t)date - 9497l) * 1440l + (int32_t)mod; }

/* ---- baked events ---- */
#define BLK 32u
#define BLK_SH 5u
static uint32_t rd32_(const uint8_t *p) {
  return (uint32_t)(p[0] | ((uint16_t)p[1] << 8)) | ((uint32_t)(p[2] | ((uint16_t)p[3] << 8)) << 16);
}
static uint8_t step_(uint8_t kind, uint8_t st, uint8_t flag) {
  switch (kind) {
  case 0: return (uint8_t)((st + 1u) & 3u);                                  /* quarters cycle */
  case 2: return (uint8_t)(st ^ 1u);                                         /* stations alternate */
  case 3: return flag ? (st ? (uint8_t)(st - 1u) : 11u) : (st == 11u ? 0u : (uint8_t)(st + 1u)); /* ingress */
  default: return 0;
  }
}

void sky_find(uint8_t s, uint32_t m, sky_ev *prev, sky_ev *next) {
  const sky_stream *S = &sky_streams[s];
  uint16_t nb = (uint16_t)((S->n + (BLK - 1u)) >> BLK_SH), lo = 0, hi = nb, mid, i, end;
  uint8_t w = S->width, fl = S->flag, kind = S->kind, f, st, k, cnt;
  uint8_t ix[10], blk[(BLK + 1u) * 4u];
  uint32_t first, at, d;
  const uint8_t *p;
  while ((uint16_t)(hi - lo) > 1u) {
    mid = (uint16_t)((lo + hi) >> 1);
    sky_copy(S->bank, S->idx + mid * 5u, 4, ix);
    if (rd32_(ix) <= m) lo = mid; else hi = mid;
  }
  sky_copy(S->bank, S->idx + lo * 5u, (uint8_t)(lo + 1u < nb ? 10u : 5u), ix);
  first = rd32_(ix);
  i = (uint16_t)(lo << BLK_SH);
  end = (uint16_t)(i + BLK); /* the next block's first event comes from its index entry */
  if (end > S->n) end = S->n;
  cnt = (uint8_t)(end - i + (end < S->n ? 1u : 0u));
  sky_copy(S->bank, S->dat + (uint16_t)(i * w), (uint8_t)(cnt * w), blk);
  p = blk;
  f = (uint8_t)(fl && (p[w - 1u] & 0x80u));
  if (first > m) {
    prev->at = SKY_NO_EVENT; prev->i = 0xFFFFu; prev->state = S->state0; prev->flag = 0;
    next->at = first; next->i = 0; next->state = ix[4]; next->flag = f;
    return;
  }
  at = first; st = ix[4];
  prev->at = at; prev->i = i; prev->state = st; prev->flag = f;
  next->at = SKY_NO_EVENT;
  for (k = 1, i++; i < end; i++, k++) {
    p += w;
    d = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
    if (w > 2u) d |= (uint32_t)p[2] << 16;
    if (w > 3u) d |= (uint32_t)(p[3] & (fl ? 0x7Fu : 0xFFu)) << 24;
    f = 0;
    if (fl && (p[w - 1u] & 0x80u)) { f = 1; if (w == 2u) d &= 0x7FFFu; else if (w == 3u) d &= 0x7FFFFFul; }
    at += d + S->bias;
    st = step_(kind, st, f);
    if (at > m) break;
    prev->at = at; prev->i = i; prev->state = st; prev->flag = f;
  }
  if (i < end) { next->at = at; next->i = i; next->state = st; next->flag = f; }
  else if (end < S->n) {
    next->at = rd32_(ix + 5u); next->i = end; next->state = ix[9];
    next->flag = (uint8_t)(fl && (blk[(uint16_t)(cnt * w) - 1u] & 0x80u));
  }
  (void)k;
}

uint8_t sky_mark(uint16_t pi, int8_t *off) {
  uint16_t lo = 0, hi = sky_nmarks, mid, v;
  uint8_t e[4];
  while (lo < hi) {
    mid = (uint16_t)((lo + hi) >> 1);
    sky_copy(sky_marks_bank, sky_marks_p + mid * 4u, 4, e);
    v = (uint16_t)(e[0] | (e[1] << 8));
    if (v == pi) { if (off) *off = (int8_t)e[3]; return e[2]; }
    if (v < pi) lo = (uint16_t)(mid + 1u); else hi = mid;
  }
  return 0;
}

uint8_t sky_aspect(uint16_t a, uint16_t b, uint16_t orb, uint16_t *miss) {
  static const uint16_t AT[SKY_NASPECT] = {0u, 16384u, 21845u, 32768u};
  uint16_t d = (uint16_t)(a - b), x;
  uint8_t k, best = 255;
  if (d > 32768u) d = (uint16_t)(0u - d);
  for (k = 0; k < SKY_NASPECT; k++) {
    x = d > AT[k] ? (uint16_t)(d - AT[k]) : (uint16_t)(AT[k] - d);
    if (x <= orb) { orb = x; best = k; }
  }
  if (miss) *miss = orb;
  return best;
}

/* ---- the sky now ---- */
static uint16_t later_[SKY_NPOS];
#ifdef SKY_HOST_TEST
uint8_t sky_force_live; /* host tests: read the sky as if outside the tables */
#endif

static void add_minutes_(uint16_t *date, uint16_t *mod, int16_t dm) {
  int16_t m = (int16_t)(*mod + dm);
  while (m < 0) { m += 1440; (*date)--; }
  while (m >= 1440) { m -= 1440; (*date)++; }
  *mod = (uint16_t)m;
}

static uint8_t near_(uint16_t a, uint16_t b, uint16_t r) {
  uint16_t d = (uint16_t)(a - b);
  return d <= r || d >= (uint16_t)(0u - r);
}

void sky_read(uint16_t date, uint16_t mod, sky_now *o) {
  int32_t m = sky_minutes(date, mod);
  uint8_t b, k, in = m >= 0 && m < (int32_t)SKY_RANGE_END;
#ifdef SKY_HOST_TEST
  if (sky_force_live) in = 0;
#endif
  uint16_t d2 = date, m2 = mod, el, g, vm, vr, rem, lim;
  uint32_t texit;
  sky_ev p, n;
  int8_t off;
  static const uint16_t AS[6] = {0u, 16384u, 21845u, 32768u, 43691u, 49152u};
  sky_positions(date, mod, o->pos);
  o->flags = in ? 0 : SKY_APPROX;
  o->eclipse = 0;
  o->retro = 0;
  o->station = 0;
  o->moon_sign = (uint8_t)(((uint32_t)o->pos[SKY_MOON] * 12u) >> 16);
  /* six hours on: speeds, for the void-of-course moon (and the live retrograde test) */
  add_minutes_(&d2, &m2, 360);
  sky_positions(d2, m2, later_);
  if (in) {
    sky_find(SKY_S_PHASE, (uint32_t)m, &p, &n);
    if (p.at == SKY_NO_EVENT) o->phase8 = (uint8_t)((p.state * 2u + 1u) & 7u); /* before the first baked quarter */
    else {
      uint32_t span = n.at - p.at, f = (uint32_t)m - p.at;
      o->phase8 = (uint8_t)((p.state * 2u + (f * 4u >= span * 3u ? 2u : f * 4u >= span ? 1u : 0u)) & 7u);
      for (k = 0; k < 2; k++) {
        sky_ev *e = k ? &n : &p;
        uint32_t gap = e->at > (uint32_t)m ? e->at - (uint32_t)m : (uint32_t)m - e->at;
        uint8_t f8;
        if (e->at == SKY_NO_EVENT || (e->state & 1u) || gap > 720u) continue;
        f8 = sky_mark(e->i, &off);
        if (SKY_ECL_KIND(f8)) { o->eclipse = SKY_ECL_KIND(f8); o->flags |= SKY_ECLIPSE; }
        if (f8 & SKY_SUPER) o->flags |= SKY_SUPERM;
      }
    }
    for (b = 0; b < 5u; b++) {
      sky_find((uint8_t)(SKY_S_ST_MERCURY + b), (uint32_t)m, &p, &n);
      if (p.state) o->retro |= (uint8_t)(1u << (b + SKY_MERCURY));
      if ((p.at != SKY_NO_EVENT && (uint32_t)m - p.at < 1440u) || (n.at != SKY_NO_EVENT && n.at - (uint32_t)m < 1440u))
        o->station |= (uint8_t)(1u << (b + SKY_MERCURY));
    }
  } else {
    /* the live model alone: phase from the elongation, retrograde from the motion over two days (t-1 d .. t+1 d,
     * so the slow planets' rounding noise cannot flip it), eclipses from the mean node */
    uint16_t back[SKY_NPOS], fwd[SKY_NPOS], dn, sn;
    d2 = date; m2 = mod; add_minutes_(&d2, &m2, -1440);
    sky_positions(d2, m2, back);
    d2 = date; m2 = mod; add_minutes_(&d2, &m2, 1440);
    sky_positions(d2, m2, fwd);
    el = (uint16_t)(o->pos[SKY_MOON] - o->pos[SKY_SUN]);
    o->phase8 = (uint8_t)(((uint16_t)(el + 4096u)) >> 13);
    for (b = SKY_MERCURY; b < SKY_NPLANET; b++) {
      if ((int16_t)(fwd[b] - back[b]) < 0) o->retro |= (uint8_t)(1u << b);
      if (((int16_t)(o->pos[b] - back[b]) < 0) != ((int16_t)(fwd[b] - o->pos[b]) < 0)) o->station |= (uint8_t)(1u << b);
    }
    /* a new or full moon within about 12 h (the Moon gains 6 deg on the Sun in 12 h) near the node: an eclipse.
     * Rough kinds from the distance to the node: Sun within 10.5 deg = central (total or annular), 17 = partial;
     * Moon within 5 = total, 11.5 = partial (umbral), 17 = penumbral. */
    dn = (uint16_t)(o->pos[SKY_SUN] - o->pos[SKY_NODE]);
    if (dn > 0x8000u) dn = (uint16_t)(0u - dn);
    if (dn > 0x4000u) dn = (uint16_t)(0x8000u - dn); /* distance to the nearer node, 0..90 deg */
    sn = dn; /* at a full moon the Moon sits opposite the Sun: the same distance from the other node */
    if (near_(el, 0, SKY_DEG(6)) && sn < SKY_DEG(17)) {
      o->eclipse = sn < (uint16_t)(SKY_DEG(21) / 2u) ? 3u : 1u;
      o->flags |= SKY_ECLIPSE;
    } else if (near_(el, 0x8000u, SKY_DEG(6)) && sn < SKY_DEG(17)) {
      o->eclipse = sn < SKY_DEG(5) ? 7u : sn < (uint16_t)(SKY_DEG(23) / 2u) ? 6u : 5u;
      o->flags |= SKY_ECLIPSE;
    }
  }
  /* void of course: no major aspect from the Moon to Sun..Saturn perfects before it leaves its sign */
  vm = (uint16_t)(later_[SKY_MOON] - o->pos[SKY_MOON]); /* per 6 h, about 600 */
  rem = (uint16_t)((uint16_t)(((uint32_t)(o->moon_sign + 1u) * 65536u + 11u) / 12u) - o->pos[SKY_MOON]);
  texit = (uint32_t)rem * 360u / vm; /* minutes */
  o->voc_end = (uint32_t)(m + (int32_t)texit);
  for (b = SKY_SUN; b < SKY_NPLANET; b++) {
    if (b == SKY_MOON) continue;
    vr = (uint16_t)(vm - (uint16_t)(later_[b] - o->pos[b]));
    lim = (uint16_t)((uint32_t)vr * texit / 360u); /* how far the Moon gains on it before the exit */
    el = (uint16_t)(o->pos[SKY_MOON] - o->pos[b]);
    for (k = 0; k < 6u; k++) {
      g = (uint16_t)(AS[k] - el);
      if (g <= lim) goto aspected;
    }
  }
  o->flags |= SKY_VOC;
aspected:;
}

#ifdef SKY_PROFILE
uint16_t sky_prof_mean(uint8_t i, uint16_t d, uint16_t m) { return mean_(i, d, m); }
int32_t sky_prof_series(uint8_t t0, uint8_t n) { return series_(t0, n); }
uint16_t sky_prof_lon(uint8_t b) { return lon_(b); }
int16_t sky_prof_rad(uint8_t b) { return rad11_(b); }
#endif
