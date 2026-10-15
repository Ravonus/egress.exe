/* The run planner (see eg_plan.h). Shared verbatim by the cartridge and the host curriculum sim (host/egplan.c). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_plan.h"

const uint8_t eg_plan_order[MK_COUNT - 2u] = {MK_BURN, MK_COLD, MK_GLASS, MK_STEPS, MK_SHORT, MK_WEIGHT, MK_RHYTHM};

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
  for (k = 0u; k != MK_COUNT; k++) ps->bias[k] = 0;
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

uint8_t eg_plan_floor(eg_plan_state *ps, uint8_t floor, eg_seed_rec *rec) EG_BANKED {
  eg_seed_rec e;
  uint8_t pos = (uint8_t)((floor - 1u) % 3u), stage, m = 255u, k, next = 255u, pass, lvl, tgt, n, ok, g;
  uint16_t i, allowed, best = 0u, cost, best_cost = 0xffffu, own;
  for (k = 0u; k != (uint8_t)(MK_COUNT - 2u); k++)
    if (!(ps->seen & (1u << eg_plan_order[k]))) {
      next = eg_plan_order[k];
      break;
    }
  g = ps->generation > 8u ? 8u : ps->generation;
  if (ps->develop != 255u) {
    stage = ST_DEVELOP;
    m = ps->develop;
  } else if (next != 255u && (pos == 0u || ps->seen == MK_BASIC)) {
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
    for (m = MK_BURN; m != MK_COUNT; m++)
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
      {
        int8_t t = (int8_t)((int8_t)tgt + ps->bias[k]);
        cost = (uint16_t)(absd(lvl, t < 0 ? 0u : (uint8_t)t) * 8u);
      }
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

void eg_plan_result(eg_plan_state *ps, uint8_t resets, uint16_t frames) EG_BANKED {
  eg_seed_rec e;
  uint8_t k;
  eg_seed_get(ps->pick, &e);
  k = MK_TRICK(e.trick);
  if (resets >= 2u) {
    if (ps->bias[k] > -EG_PLAN_BIAS) ps->bias[k]--;
  } else if (!resets && frames && frames < (uint16_t)(60u * (30u + (uint16_t)(e.diff >> 4) * 8u))) {
    if (ps->bias[k] < EG_PLAN_BIAS) ps->bias[k]++;
  }
}
