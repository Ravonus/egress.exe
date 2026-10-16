/* The run planner (see eg_plan.h). Shared verbatim by the cartridge and the host curriculum sim (host/egplan.c). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_plan.h"
#include "eg_stage.h" /* B2: the deck */

const uint8_t eg_plan_order[MK_SLICE_COUNT - 2u] = {MK_SHORT, MK_COLD, MK_WEIGHT, MK_GLASS, MK_STEPS, MK_RHYTHM, MK_BURN,
    MK_SLICE_RUSH, MK_SLICE_STEER, MK_SLICE_MIRROR, MK_SLICE_STARE};

static uint8_t popc(uint16_t m) {
  uint8_t n = 0u;
  while (m) {
    n += (uint8_t)(m & 1u);
    m >>= 1;
  }
  return n;
}

static uint8_t mix8(uint16_t a, uint16_t b) {
  uint16_t x = (uint16_t)(a ^ (uint16_t)(b * 0x9e37u));
  x ^= x << 7;
  x ^= x >> 9;
  x ^= x << 8;
  return (uint8_t)(x ^ (x >> 8));
}

static uint8_t absd(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }

void eg_plan_begin(eg_plan_state *ps, uint16_t run_seed, uint16_t seen, uint8_t generation) EG_BANKED {
  uint8_t k;
  ps->seen = (uint16_t)(seen | MK_BASIC);
  ps->run_seed = run_seed;
  ps->generation = generation;
  ps->lineage = 0u;
  for (k = 0u; k != MK_SLICE_COUNT; k++) ps->bias[k] = 0, ps->streak[k] = 0u;
  ps->prev_trick = ps->prev_level = ps->develop = 255u;
  ps->stage = ps->target = 0u;
  ps->mech_new = 255u;
  ps->pick = 0u;
}

uint8_t eg_plan_target(const eg_plan_state *ps, uint8_t floor) EG_BANKED {
  static const int8_t wave[3] = {1, -1, 3}; /* tension, release, spike (the landing after is the rest) */
  uint8_t g = ps->generation > 8u ? 8u : ps->generation;
  int8_t t = (int8_t)(2 + (int8_t)(((floor - 1u) * 3u) >> 2) + (int8_t)(g >> 1) + (int8_t)ps->lineage +
                      wave[(floor - 1u) % 3u]);
  return t < 0 ? 0u : (t > 15 ? 15u : (uint8_t)t);
}

static uint8_t plan_floor(eg_plan_state *ps, uint8_t floor, eg_seed_rec *rec, uint8_t lesson) {
  eg_seed_rec e;
  uint8_t pos = (uint8_t)((floor - 1u) % 3u), stage, m = 255u, k, next = 255u, pass, lvl, tgt, n, ok, g;
  uint16_t i, allowed, best = 0u, cost, best_cost = 0xffffu, own;
  for (k = 0u; k != (uint8_t)(MK_SLICE_COUNT - 2u); k++)
    if (!(ps->seen & (1u << eg_plan_order[k]))) {
      next = eg_plan_order[k];
      break;
    }
  g = ps->generation > 8u ? 8u : ps->generation;
  if (ps->develop != 255u && !lesson) {
    stage = ST_DEVELOP;
    m = ps->develop;
  } else if (next != 255u && (lesson || pos == 0u || ps->seen == MK_BASIC)) {
    stage = ST_INTRO;
    m = next;
  } else if (pos == 2u)
    stage = ST_TEST;
  else if (mix8(ps->run_seed, (uint16_t)(floor * 31u)) % 100u <
           (uint8_t)(10u + floor * 7u + g * 3u > 85u ? 85u : 10u + floor * 7u + g * 3u))
    stage = ST_COMBINE;
  else {
    stage = ST_DEVELOP; /* a seen mechanic, on its own */
    n = popc((uint16_t)(ps->seen & ~MK_BASIC));
    k = (uint8_t)(mix8(ps->run_seed, (uint16_t)(floor * 57u + 3u)) % n);
    for (m = MK_BURN; m != MK_SLICE_COUNT; m++)
      if ((ps->seen & (1u << m)) && !k--) break;
  }
  tgt = eg_plan_target(ps, floor);
  if (stage == ST_INTRO) tgt = 2u; /* gentle: one clear aha */
  if (stage == ST_DEVELOP && ps->develop != 255u && tgt < ps->prev_level + 1u) tgt = (uint8_t)(ps->prev_level + 1u);
  allowed = (uint16_t)(ps->seen | (stage == ST_INTRO ? (1u << m) : 0u));
  for (pass = 0u; pass != 3u; pass++) {
    for (i = 0u; i != EG_SEEDS; i++) {
      eg_seed_get(i, &e);
      if (e.mech & ~allowed) continue; /* never a mechanic the player hasn't met */
      own = (uint16_t)(e.mech & ~MK_BASIC);
      n = popc((uint16_t)(own & ~(1u << MK_STEPS))); /* trick mechanics; step-count routing is a twist on top */
      ok = 1u;
      if (stage == ST_INTRO)
        ok = m == MK_STEPS ? (uint8_t)((own & (1u << MK_STEPS)) && n == 1u) : (uint8_t)(own == (1u << m));
      else if (stage == ST_DEVELOP)
        ok = (uint8_t)((m == MK_STEPS ? (own & (1u << MK_STEPS)) != 0u : MK_TRICK(e.trick) == m) &&
                       (pass || n == 1u));
      else if (pass == 0u)
        ok = (uint8_t)(n >= 2u);
      if (pass == 2u) ok = 1u; /* last resort: anything the player has met */
      if (!ok) continue;
      lvl = (uint8_t)(e.diff >> 4);
      k = MK_TRICK(e.trick);
      /* the capped nudge: an introduction is never softened; anything else at most EG_PLAN_SOFT under the plan */
      cost = (uint16_t)(absd(lvl, eg_nudge_level(tgt, ps->bias[k],
                                                 stage == ST_INTRO ? tgt
                                                 : tgt > EG_PLAN_SOFT ? (uint8_t)(tgt - EG_PLAN_SOFT) : 0u)) * 8u);
      cost += (uint16_t)(mix8((uint16_t)(ps->run_seed + floor), i) & 7u);
      if (e.trick == ps->prev_trick && lvl == ps->prev_level) cost += 200u; /* never the same twice running */
      if (stage == ST_TEST) cost -= (uint16_t)(cost >= (uint16_t)(e.insights * 2u) ? e.insights * 2u : cost);
      if (cost < best_cost) {
        best_cost = cost;
        best = i;
      }
    }
    if (best_cost != 0xffffu) break;
  }
  eg_seed_get(best, rec);
  ps->pick = best;
  ps->stage = stage;
  ps->target = tgt;
  ps->mech_new = 255u;
  ps->develop = 255u;
  if (stage == ST_INTRO) {
    ps->seen |= (uint16_t)(1u << m);
    ps->develop = m;
    ps->mech_new = m;
  }
  ps->prev_trick = rec->trick;
  ps->prev_level = (uint8_t)(rec->diff >> 4);
  return stage;
}

uint8_t eg_plan_floor(eg_plan_state *ps, uint8_t floor, eg_seed_rec *rec) EG_BANKED {
  return plan_floor(ps,floor,rec,0u);
}
uint8_t eg_plan_intro(eg_plan_state *ps, uint8_t floor, eg_seed_rec *rec) EG_BANKED {
  return plan_floor(ps,floor,rec,1u);
}

void eg_plan_result(eg_plan_state *ps, uint8_t resets, uint16_t frames) EG_BANKED {
  eg_seed_rec e;
  uint8_t k;
  eg_seed_get(ps->pick, &e);
  k = MK_TRICK(e.trick);
  eg_nudge_result(&ps->bias[k], &ps->streak[k], (uint8_t)(resets >= 2u || !frames), (uint8_t)(!resets && frames),
                  (uint8_t)(!resets && frames && frames < (uint16_t)(60u * (30u + (uint16_t)(e.diff >> 4) * 8u))));
}

/* --- the capped nudge (3.6.8) --- */
uint8_t eg_nudge_level(uint8_t target, int8_t bias, uint8_t floor_min) EG_BANKED {
  int8_t t;
  if (bias < -EG_PLAN_SOFT) bias = -EG_PLAN_SOFT;
  if (bias > EG_PLAN_HARD) bias = EG_PLAN_HARD;
  t = (int8_t)((int8_t)target + bias);
  if (t < (int8_t)floor_min) t = (int8_t)floor_min;
  return t > 15 ? 15u : (uint8_t)t;
}

void eg_nudge_result(int8_t *bias, uint8_t *streak, uint8_t failed, uint8_t clean, uint8_t fast) EG_BANKED {
  if (failed) {
    if (*streak != 255u) (*streak)++;
    if (*streak >= EG_PLAN_STREAK && *bias > -EG_PLAN_SOFT) {
      (*bias)--;
      *streak = 0u; /* the next softening needs another run of failures (and the cap holds it anyway) */
    }
    return;
  }
  *streak = 0u;
  if (clean && *bias < 0) *bias = 0; /* back on plan */
  else if (fast && *bias < EG_PLAN_HARD) (*bias)++;
}

/* ===================== The full run (eg_plan.h; docs/redesign.md 3.6) =====================
 * Compiled on the host always; on the cartridge only with EG_FULL_RUN (B4, when the other biomes exist). It builds
 * under sdcc at 7.2 KB of bank code; the B1 cart plays the slice above and leaves it out. */
#if !defined(__SDCC) || defined(EG_FULL_RUN)

/* the biome position each mechanic arrives at: 2 in the first biome, 3 a biome through the middle, then fewer, and the
 * glitch core's one twist. Introductions spread over the whole run; the late biomes mostly combine. */
static const uint8_t band0[MK_ALL] = {255u, 255u, /* BURN */ 0u, /* COLD */ 1u, /* SHORT */ 2u, /* WEIGHT */ 3u,
                                      /* GLASS */ 0u, /* RHYTHM */ 4u, /* STEPS */ 1u, /* KETTLE */ 2u, /* BEAM */ 4u,
                                      /* DARK */ 5u, /* PATROL */ 1u, /* HEAVY */ 2u, /* SLEEPER */ 3u,
                                      /* PUSHER */ 4u, /* EATER */ 5u, /* SENTRY */ 3u, /* MIRROR */ 5u,
                                      /* FOLLOWER */ 6u, /* LIGHTEATER */ 6u, /* DRIFTER */ 7u};
/* each mechanic's home biome: it is weighted up in that biome's combines */
static const uint8_t home[MK_ALL] = {BI_OFFICE, BI_OFFICE, BI_OFFICE, BI_ATRIUM, BI_SERVER, BI_MALL, BI_OFFICE,
                                     BI_CORE, BI_VENTS, BI_VENTS, BI_ROOFTOP, BI_SERVER, BI_OFFICE, BI_OFFICE,
                                     BI_VENTS, BI_MALL, BI_MALL, BI_SERVER, BI_GARDEN, BI_GARDEN, BI_ROOFTOP,
                                     BI_CORE};

static uint8_t mix3(uint16_t a, uint16_t b, uint16_t c) { return mix8((uint16_t)(a ^ (uint16_t)(c * 0x2545u)), b); }

void eg_run_world(eg_run *r, uint16_t cycle_seed, uint8_t cycle) EG_BANKED {
  uint8_t p, k, j, t, b, pool[BI_COUNT];
  /* the first biome is the office or the vents, the glitch core is last, every biome once */
  r->order[0] = (cycle_seed & 1u) ? BI_VENTS : BI_OFFICE;
  r->order[BI_COUNT - 1u] = BI_CORE;
  k = 0u;
  for (b = 0u; b != BI_CORE; b++)
    if (b != r->order[0]) pool[k++] = b;
  for (j = (uint8_t)(k - 1u); j; j--) { /* Fisher-Yates over positions 1-6 */
    t = (uint8_t)(mix3(cycle_seed, 40u + j, cycle) % (uint8_t)(j + 1u));
    b = pool[j];
    pool[j] = pool[t];
    pool[t] = b;
  }
  for (j = 0u; j != k; j++) r->order[1u + j] = pool[j];
  /* stage slots: 4 in the first two biomes and the glitch core, 4-6 between (mean 4.5) */
  for (p = 0u; p != BI_COUNT; p++) {
    t = (uint8_t)(mix3(cycle_seed, 100u + p, cycle) % 10u);
    r->stages[p] = (p < 2u || p == BI_COUNT - 1u) ? 4u : t < 6u ? 4u : t < 9u ? 5u : 6u;
  }
  /* the bands; a new cycle moves one mechanic across each band boundary, so the combos arrive in a new order */
  for (k = 0u; k != MK_ALL; k++) r->band[k] = band0[k];
  if (cycle > 1u)
    for (p = 1u; p != (uint8_t)(BI_COUNT - 2u); p++) {
      uint8_t a = 255u, c = 255u, na = 0u, nc = 0u;
      for (k = MK_BURN; k != MK_ALL; k++) {
        if (r->band[k] == p && !(mix3(cycle_seed, (uint16_t)(k * 7u + p), cycle) % (uint8_t)(++na))) a = k;
        if (r->band[k] == p + 1u && !(mix3(cycle_seed, (uint16_t)(k * 11u + p), cycle) % (uint8_t)(++nc))) c = k;
      }
      if (a != 255u && c != 255u) {
        r->band[a] = (uint8_t)(p + 1u);
        r->band[c] = p;
      }
    }
}

static uint8_t clamp15(int16_t v) { return v < 0 ? 0u : v > 15 ? 15u : (uint8_t)v; }

/* biome base level by position: 2 3 5 6 8 9 11 12 */
#define BASE(p) ((int16_t)(2 + ((3 * (int16_t)(p) + 1) >> 1)))

typedef struct {
  const eg_run_in *in;
  eg_run *r;
  uint32_t intro, devel;  /* introduced and developed: the line's, plus this run's */
  uint8_t pend;           /* introduced last stage slot, developed next (255: none) */
  uint8_t prev_type, prev_rare, prev_lvl, rare_biome, sky_left, floor;
  uint32_t prev_set;      /* the last stage slot's mechanics */
  int16_t press;          /* generation, cycle and lineage on top of the depth */
  eg_deck deck;           /* B2: the stage types dealt and the tricks played (eg_stage.h) */
} bstate;

/* B2: the weighted deck of section 4 (src/core/eg_stage.c), with its seen history */
static uint8_t pick_type(bstate *b, uint8_t cstage, uint8_t m, uint8_t p, uint8_t salt) {
  return eg_deck_deal(&b->deck, p, cstage, m, (uint8_t)((b->devel & (1ul << MK_DARK)) && b->r->band[MK_DARK] <= p),
                      mix3(b->in->run_seed, (uint16_t)(b->floor * 13u + salt), 7u));
}

/* 2 or 3 developed mechanics open at this depth, weighted toward the biome's own; never the last slot's set */
static uint32_t set_of(const uint8_t *m) {
  uint32_t v = 0u;
  uint8_t i;
  for (i = 0u; i != 3u; i++)
    if (m[i] != 255u) v |= 1ul << m[i];
  return v;
}

static uint8_t pick_set(bstate *b, uint8_t p, uint8_t biome, uint8_t want, uint8_t *out, uint8_t salt) {
  uint8_t w[MK_ALL], k, n, got = 0u, x, tries;
  uint16_t tot;
  for (tries = 0u; tries != 4u; tries++) {
    got = 0u;
    for (k = 0u; k != MK_ALL; k++) {
      w[k] = 0u;
      if (k >= MK_BURN && (b->devel & (1ul << k)) && b->r->band[k] <= p)
        w[k] = (uint8_t)(1u + (b->r->band[k] == p ? 3u : 0u) + (home[k] == biome ? 2u : 0u) +
                         (b->r->band[k] + 1u == p ? 1u : 0u));
    }
    for (n = 0u; n != want; n++) {
      tot = 0u;
      for (k = 0u; k != MK_ALL; k++) tot = (uint16_t)(tot + w[k]);
      if (!tot) break;
      x = (uint8_t)(((uint16_t)mix3(b->in->run_seed, (uint16_t)(b->floor * 29u + n * 5u + tries * 71u + salt),
                                    (uint16_t)(b->in->cycle + 3u)) *
                     tot) >> 8);
      for (k = 0u; k != MK_ALL; k++) {
        if (x < w[k]) break;
        x = (uint8_t)(x - w[k]);
      }
      if (k == MK_ALL) break;
      out[got++] = k;
      w[k] = 0u;
    }
    for (k = got; k != 3u; k++) out[k] = 255u;
    if (got < 2u || set_of(out) != b->prev_set) break; /* never the same mix twice running */
  }
  return got;
}

static eg_slot *add(bstate *b, uint8_t kind, uint8_t biome, uint8_t p) {
  eg_slot *s = &b->r->s[b->r->n++];
  s->kind = kind;
  s->stype = SG_PUZZLE;
  s->cstage = ST_COMBINE;
  s->biome = biome;
  s->bpos = p;
  s->m[0] = s->m[1] = s->m[2] = 255u;
  s->twist = 0u;
  s->target = s->floor_min = 0u;
  if (kind != SK_LANDING) b->floor++, b->r->floors++;
  s->floor = b->floor;
  return s;
}

static uint8_t twist_for(bstate *b, uint8_t p, uint8_t salt) {
  uint8_t pc = (uint8_t)(p > 2u ? (p - 2u) * 12u : 0u), n, t = 0u, i;
  pc = (uint8_t)(pc + (b->in->cycle > 1u ? (b->in->cycle - 1u) * 20u : 0u));
  if (b->in->mode == RM_DESCENT) pc = 100u;
  if (pc > 85u && b->in->mode != RM_DESCENT) pc = 85u;
  if (mix3(b->in->run_seed, (uint16_t)(b->floor * 37u + salt), 11u) % 100u >= pc) return 0u;
  /* descent stacks themes as it goes down: 1, then 2 from stratum 4, 3 from stratum 8 */
  n = b->in->mode == RM_DESCENT ? (uint8_t)(1u + (b->in->stratum >= 4u) + (b->in->stratum >= 8u)) : 1u;
  for (i = 0u; i != n; i++) t |= (uint8_t)(1u << (mix3(b->in->run_seed, (uint16_t)(b->floor * 41u + i * 3u), 13u) % 7u));
  return t;
}

/* one biome's block: stage slots under the curriculum, the mid-boss after the first half, the boss */
static void biome_block(bstate *b, uint8_t p, uint8_t biome, uint8_t S, int16_t base, uint8_t descent) {
  uint8_t j, h = (uint8_t)((S + 1u) >> 1), k, m, cst, due, undev, lvl;
  int16_t off;
  eg_slot *s;
  for (j = 0u; j != S; j++) {
    if (!descent && j == h) {
      s = add(b, SK_MIDBOSS, biome, p);
      s->stype = SG_HUNT;
      s->target = s->floor_min = clamp15(base + b->press + 2);
    }
    m = 255u;
    /* the next unseen mechanic due here (lowest band first, then this biome's own), and one introduced earlier by the
     * line but never developed */
    due = undev = 255u;
    for (k = MK_BURN; k != MK_ALL; k++) {
      if (b->r->band[k] > p) continue;
      if (!(b->intro & (1ul << k))) {
        if (due == 255u || b->r->band[k] < b->r->band[due] ||
            (b->r->band[k] == b->r->band[due] && home[k] == biome && home[due] != biome))
          due = k;
      } else if (!(b->devel & (1ul << k)) && k != b->pend && undev == 255u)
        undev = k;
    }
    if (b->pend != 255u) {
      cst = ST_DEVELOP, m = b->pend;
    } else if (!descent && due != 255u && j + 1u < S) {
      cst = ST_INTRO, m = due; /* room for its develop before the biome ends */
    } else if (undev != 255u && j + 1u < S) {
      cst = ST_DEVELOP, m = undev;
    } else if (j + 1u == S)
      cst = ST_TEST;
    else if (!descent && mix3(b->in->run_seed, (uint16_t)(b->floor * 23u), 3u) % 100u < (uint8_t)(p < 5u ? 40u - p * 6u : 10u))
      cst = ST_DEVELOP; /* a known mechanic alone, a notch harder (more often early, where few are open) */
    else
      cst = ST_COMBINE;
    s = add(b, SK_STAGE, biome, p);
    s->cstage = cst;
    /* the wave: the biome opens on a release, rises to the mid-boss, releases, then the test spikes before the boss */
    off = j < h ? (int16_t)(j > 2u ? 1 : (int16_t)j - 1) : j == h ? -1 : j + 1u == S ? 2 : (int16_t)((j - h) & 1u);
    s->target = clamp15(base + b->press + off);
    s->floor_min = clamp15(base + b->press - 1); /* the curriculum floor for this depth */
    if (cst == ST_INTRO) {
      s->m[0] = m;
      s->target = s->floor_min = EG_INTRO_LEVEL(p);
      b->intro |= 1ul << m;
      b->pend = m;
    } else if (cst == ST_DEVELOP && m == 255u) {
      /* a seen mechanic alone: this depth's, or the biome's own; never the last slot's */
      uint8_t n = 0u, x;
      for (k = MK_BURN; k != MK_ALL; k++)
        if ((b->devel & (1ul << k)) && b->r->band[k] <= p && !(b->prev_set == (1ul << k) || b->prev_set == (MK_BASIC | (1ul << k))))
          n++;
      if (!n) {
        s->cstage = cst = ST_COMBINE;
        pick_set(b, p, biome, 2u, s->m, j);
        s->twist = twist_for(b, p, j);
      } else {
        x = (uint8_t)(mix3(b->in->run_seed, (uint16_t)(b->floor * 31u + 7u), 9u) % n);
        for (k = MK_BURN; k != MK_ALL; k++)
          if ((b->devel & (1ul << k)) && b->r->band[k] <= p && !(b->prev_set == (1ul << k) || b->prev_set == (MK_BASIC | (1ul << k))) && !x--)
            break;
        s->m[0] = k;
      }
    } else if (cst == ST_DEVELOP) {
      s->m[0] = m;
      /* the first develop of a mechanic: a notch over its introduction, not the biome's full level */
      lvl = clamp15((int16_t)EG_INTRO_LEVEL(p) + 2 + (int16_t)(p >> 1) + (b->press >> 1));
      if (s->target > lvl) s->target = lvl;
      if (s->target < EG_INTRO_LEVEL(p) + 2u) s->target = (uint8_t)(EG_INTRO_LEVEL(p) + 2u);
      if (s->floor_min > s->target) s->floor_min = s->target;
      b->devel |= 1ul << m;
      b->pend = 255u;
    } else {
      uint8_t want = 2u, pc3 = (uint8_t)(p >= 3u ? (p - 2u) * 10u : 0u);
      if (b->in->cycle > 1u) pc3 = (uint8_t)(pc3 + 20u);
      if (cst == ST_TEST) pc3 = (uint8_t)(pc3 + 15u);
      if (descent) pc3 = (uint8_t)(40u + b->in->stratum * 5u);
      if (mix3(b->in->run_seed, (uint16_t)(b->floor * 17u), 5u) % 100u < pc3) want = 3u;
      if (pick_set(b, p, biome, want, s->m, j) < 2u) {
        /* too few developed mechanics for a mix: the strongest single one, at the slot's level */
        s->cstage = cst == ST_TEST ? ST_TEST : ST_DEVELOP;
        for (k = MK_ALL - 1u; k >= MK_BURN; k--)
          if ((b->devel & (1ul << k)) && b->r->band[k] <= p) break;
        s->m[0] = k >= MK_BURN ? k : 255u;
        s->m[1] = s->m[2] = 255u;
      }
      s->twist = twist_for(b, p, j);
    }
    s->stype = pick_type(b, s->cstage, s->m[0], p, j);
    /* rare stages take a combine slot: a 3D stage, a glitch rift, or today's sky event (once a run) */
    if (s->cstage == ST_COMBINE && p >= 1u && !b->prev_rare && b->rare_biome != p) {
      uint8_t x = (uint8_t)(mix3(b->in->run_seed, (uint16_t)(b->floor * 53u), 17u) % 100u);
      if (b->sky_left && x < 30u) s->stype = SG_SKY, b->sky_left = 0u;
      else if (x >= 90u) s->stype = SG_3D;
      else if (x >= 84u && p >= 2u) s->stype = SG_RIFT;
    }
    b->prev_rare = SG_RARE(s->stype);
    if (b->prev_rare) b->rare_biome = p;
    b->prev_type = s->stype;
    eg_deck_note(&b->deck, s->stype, s->m[0]); /* B2 */
    b->prev_lvl = s->target;
    b->prev_set = set_of(s->m);
  }
  s = add(b, SK_BOSS, biome, p);
  s->stype = SG_HUNT;
  s->target = s->floor_min = clamp15(base + b->press + 3);
}

void eg_run_build(eg_run *r, const eg_run_in *in) EG_BANKED {
  bstate b;
  uint8_t p, q;
  eg_slot *s;
  eg_run_world(r, in->cycle_seed, in->cycle);
  r->n = r->floors = 0u;
  b.in = in;
  b.r = r;
  b.intro = in->seen | MK_BASIC;
  b.devel = (in->developed & in->seen) | MK_BASIC;
  b.pend = 255u;
  b.prev_type = SG_PUZZLE;
  b.prev_rare = 0u;
  b.prev_lvl = 0u;
  b.rare_biome = 255u;
  b.sky_left = in->sky;
  b.prev_set = 0u;
  eg_deck_reset(&b.deck); /* B2 */
  b.press = (int16_t)((in->generation > 24u ? 24u : in->generation) / 6u) + (int16_t)in->lineage +
            (int16_t)(in->cycle > 1u ? 2u * (in->cycle - 1u) : 0u);
  b.floor = 0u;
  if (in->mode == RM_DESCENT) {
    /* one stratum of the endless descent: five mixed stages in a biome drawn fresh (never the last stratum's), a
     * remixed boss, a landing every second stratum. The level tops out at 15; from there the pressure is themes. */
    uint8_t bi = (uint8_t)(mix3(in->run_seed, in->stratum, 19u) % BI_COUNT);
    uint8_t pb = in->stratum ? (uint8_t)(mix3(in->run_seed, (uint16_t)(in->stratum - 1u), 19u) % BI_COUNT) : 255u;
    if (bi == pb) bi = (uint8_t)((bi + 1u) % BI_COUNT);
    b.floor = (uint8_t)(in->stratum * 6u);
    biome_block(&b, BI_COUNT - 1u, bi, 5u, (int16_t)(12 + (in->stratum >> 1)) - b.press, 1u);
    if (in->stratum & 1u) add(&b, SK_LANDING, bi, BI_COUNT - 1u);
    return;
  }
  /* a shortcut door: the line has met everything behind it; the floor numbers stay the full run's */
  for (q = 0u; q != in->door; q++) b.floor = (uint8_t)(b.floor + r->stages[q] + 2u);
  r->floors = 0u;
  for (p = in->door; p != BI_COUNT; p++) {
    biome_block(&b, p, r->order[p], r->stages[p], BASE(p), 0u);
    if (p + 1u != BI_COUNT) {
      s = add(&b, SK_LANDING, r->order[p], p);
      s->target = s->floor_min = 0u;
    }
  }
  /* the final stretch: the hardest mixed room the run holds, every theme it has met, then the egress */
  s = add(&b, SK_FINAL, BI_CORE, BI_COUNT - 1u);
  s->cstage = ST_TEST;
  s->stype = SG_PUZZLE;
  pick_set(&b, BI_COUNT - 1u, BI_CORE, 3u, s->m, 99u);
  s->twist = (uint8_t)(1u << (mix3(in->run_seed, 77u, 23u) % 7u));
  s->target = s->floor_min = clamp15(BASE(BI_COUNT - 1u) + b.press + 3);
  s = add(&b, SK_EGRESS, BI_CORE, BI_COUNT - 1u);
  s->stype = SG_PUZZLE;
  s->target = s->floor_min = 0u; /* the egress asks its question; there is nothing left to solve */
}
#endif /* EG_FULL_RUN */
