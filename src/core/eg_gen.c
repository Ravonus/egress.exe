/* EGRESS room generator: the office's puzzle rooms, built backwards from the goal (docs/redesign.md 3.2).
 *
 *   EXIT <- GATE(condition) <- OBJECT(trait) <- RECIPE(a+b) <- INGREDIENTS <- OBSTACLES <- START
 *
 * 1. Deal the named trick from (seed, tier): BURN (paper barricade), COLD (a flood to freeze), SHORT (a dead shutter
 *    under the door, powered through water) or WEIGHT (a shutter under the door, a plate in a niche no CRT can reach).
 * 2. Supporting tricks (tier 2 up: 1, tier 4 up: 2) are more locks between the start and the goal: BURN or COLD.
 *    The room is cut into bands, the goal band at the top (the door's wall) and the start band at the bottom; each
 *    lock is a concrete row with one opening filled by its obstacle.
 * 3. From the goal outwards, each lock gets the object that opens it, then a recipe for that object (deeper by
 *    tier), and its ingredients go in bands strictly before the lock: what opens a lock is always reachable first.
 * 4. Red herrings (tier 1 up) are real ingredients whose recipes lead somewhere plausible but wrong.
 * 5. The bands get the physical layer (foam, CRTs, drywall, pockets), the keycard goes behind the last lock.
 *
 * At most EG_MAX_ITEM pieces per room. Deterministic in (seed, tier); 0 when the layout failed its own checks (the
 * caller reseeds). The host solver verifies every room the cartridge plays (host/eg_solve.c, tools/bake_seeds.sh). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_room.h"
#include "eg_piece.h"

static uint8_t absd(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }
static uint8_t dist(uint8_t a, uint8_t b) { return (uint8_t)(absd(EG_X(a), EG_X(b)) + absd(EG_Y(a), EG_Y(b))); }
static void settle(eg_room *r) {
  uint8_t n = 96u;
  while (n-- && (eg_gravity(r, 0, 0u) & EG_G_CHANGED)) {
  }
}

static const int8_t step4[4] = {-16, 16, -1, 1};

/* bands: zone z covers rows zt[z]..zb[z]; lock k (between zone k and k+1) is row lk[k] */
static uint8_t nb, lk[3], zt[4], zb[4], band[3];
static uint8_t need[10], nneed, needz[10], needop[10], hpiece[3], nh; /* pieces to place, the lowest zone index each may go in */

static uint8_t zone_cell(uint8_t z) {
  return EG_IDX(1u + eg_rndn(14u), zt[z] + eg_rndn((uint8_t)(zb[z] - zt[z] + 1u)));
}

/* the found pieces that make `obj` at recipe depth `d` (deeper falls back to the deepest it has) */
static uint8_t expand(uint8_t obj, uint8_t d, uint8_t *out) {
  switch (obj) {
    case P_FIRE:
      out[0] = P_FIRE;
      return 1u;
    case P_SNOW:
      if (d < 2u) {
        out[0] = P_RAIN;
        out[1] = P_AIR;
        return 2u;
      }
      out[0] = P_WATER;
      out[1] = P_AIR;
      out[2] = P_AIR;
      return 3u;
    case P_ICE: /* WATER + SNOW */
      out[0] = P_WATER;
      return (uint8_t)(1u + expand(P_SNOW, (uint8_t)(d - 1u), out + 1));
    case P_BRICK:
      out[0] = P_FIRE;
      if (d < 2u) {
        out[1] = P_MUD;
        return 2u;
      }
      out[1] = P_EARTH;
      out[2] = P_WATER;
      return 3u;
    default: /* ELECTRIC */
      out[0] = P_METAL;
      if (d < 2u) {
        out[1] = P_ENERGY;
        return 2u;
      }
      out[1] = P_AIR;
      out[2] = P_FIRE;
      return 3u;
  }
}

static uint8_t lock_obj(uint8_t trick, uint8_t deep) {
  if (trick == TRICK_BURN) return P_FIRE;
  if (trick == TRICK_COLD) return deep >= 3u ? P_ICE : P_SNOW;
  return trick == TRICK_SHORT ? P_ELECTRIC : P_BRICK;
}

/* queue a lock's ingredients: as deep as `want` allows within the item budget */
static uint8_t add_lock(uint8_t trick, uint8_t want, uint8_t minz, uint8_t *depth) {
  uint8_t buf[5], n, d = want, k;
  for (;;) {
    n = expand(lock_obj(trick, d), d, buf);
    if (nneed + n <= EG_MAX_ITEM || d == 0u) break;
    d--;
  }
  if (nneed + n > EG_MAX_ITEM) return 0u;
  /* the depth reached: chains of found pieces */
  k = trick == TRICK_BURN ? 0u : (d >= 3u && trick == TRICK_COLD ? 3u : (d >= 2u ? 2u : 1u));
  if (k > *depth) *depth = k;
  for (k = 0u; k != n; k++) {
    need[nneed] = buf[k];
    needz[nneed] = minz;
    needop[nneed] = (uint8_t)(n == 1u); /* the object itself opens the lock */
    nneed++;
  }
  return 1u;
}

/* a band reads floor first: open carpet, a few concrete runs (not in the goal band), and insulation foam in a couple
 * of deliberate patches. CRTs and drywall are not scattered: guard() places them where they matter. */
static void fill_zone(eg_room *r, uint8_t z, uint8_t tier) {
  uint8_t *c = r->c;
  uint8_t k, n, len, s, t, x, y, h = (uint8_t)(zb[z] - zt[z] + 1u);
  for (y = zt[z]; y <= zb[z]; y++)
    for (x = 1u; x != EG_W - 1u; x++) c[EG_IDX(x, y)] = T_EMPTY;
  if (h >= 3u && z) { /* no concrete in the goal band: the door, the plate niche and the keycard stay open */
    n = (uint8_t)(1u + (tier >> 2));
    for (k = 0u; k != n; k++) {
      len = (uint8_t)(2u + eg_rndn(3u));
      s = zone_cell(z);
      while (len-- && EG_X(s) >= 1u && EG_X(s) <= EG_W - 2u) {
        c[s] = T_WALL;
        s++;
      }
    }
  }
  n = (uint8_t)(1u + h / 3u);
  for (k = 0u; k != n; k++) {
    s = zone_cell(z);
    len = (uint8_t)(3u + eg_rndn(4u));
    while (len--) {
      if (c[s] == T_EMPTY) c[s] = T_DIRT;
      t = (uint8_t)(s + step4[eg_rnd() & 3u]);
      x = EG_X(t);
      y = EG_Y(t);
      if (x != 0u && x < EG_W - 1u && y >= zt[z] && y <= zb[z]) s = t;
    }
  }
}

/* the physical layer, placed for a reason around something the plan needs (a piece or a keycard at `i`):
 *   0  a CRT resting on it: take it and you hold the set up; step out from under it the wrong way and it lands on you
 *   1  an alcove: concrete on three sides, drywall on the fourth (break it, or come round)
 *   2  a CRT up on a foam ledge over it: dig the ledge out from below and it drops
 *   3  buried: foam on its open sides */
static uint8_t open_cell(uint8_t v) { return (uint8_t)(v == T_EMPTY || v == T_DIRT); }
static void guard(eg_room *r, uint8_t i, uint8_t kind) {
  uint8_t *c = r->c;
  uint8_t d, s, y = EG_Y(i), side;
  if (y < 2u) return;
  if (kind == 0u) {
    s = (uint8_t)(i - EG_W);
    if (open_cell(c[s])) c[s] = T_ROCK;
  } else if (kind == 1u) {
    side = (uint8_t)(eg_rnd() & 3u);
    for (d = 0u; d != 4u; d++) {
      s = (uint8_t)(i + step4[d]);
      if (!open_cell(c[s]) || s == r->start || EG_Y(s) == 0u || EG_Y(s) == EG_H - 1u) continue;
      c[s] = d == side ? T_BRICK : T_WALL;
    }
  } else if (kind == 2u) {
    s = (uint8_t)(i - EG_W);
    if (y >= 3u && open_cell(c[s]) && open_cell(c[(uint8_t)(s - EG_W)])) {
      c[s] = T_DIRT;
      c[(uint8_t)(s - EG_W)] = T_ROCK;
    }
  } else
    for (d = 0u; d != 4u; d++) {
      s = (uint8_t)(i + step4[d]);
      if (c[s] == T_EMPTY && s != r->start) c[s] = T_DIRT;
    }
}

/* a free cell in zones >= minz for a piece, a keycard or a token */
static uint8_t free_cell(eg_room *r, uint8_t minz, uint8_t maxz, uint8_t avoid) {
  uint8_t tries, z, i, t;
  for (tries = 0u; tries != 80u; tries++) {
    z = (uint8_t)(minz + eg_rndn((uint8_t)(maxz - minz + 1u)));
    i = zone_cell(z);
    t = r->c[i];
    if ((t != T_DIRT && t != T_EMPTY) || i == r->start || dist(i, r->start) < avoid ||
        i == (uint8_t)(r->door + EG_W) || i == (uint8_t)(r->door + 2u * EG_W))
      continue;
    return i;
  }
  return 0u;
}

uint8_t eg_generate(eg_room *r, uint16_t seed, uint8_t tier) EG_BANKED {
  uint8_t *c = r->c;
  uint8_t i, k, n, x, y, t, s, w, a, tries, d, nsup, herr, depth, xn;
  eg_rs = (uint16_t)(seed ^ 0x5a3cu ^ ((uint16_t)tier << 11));
  if (!eg_rs) eg_rs = 0x1234u;
  eg_rnd();
  eg_rnd();
  eg_rnd();
  for (i = 0u; i != (uint8_t)EG_N; i++) {
    x = EG_X(i);
    y = EG_Y(i);
    c[i] = (x == 0u || y == 0u || x == EG_W - 1u || y == EG_H - 1u) ? T_WALL : T_DIRT;
  }
  r->keys = r->tokens = r->nspawn = r->nitem = 0u;
  for (k = 0u; k != EG_MAX_SPAWN; k++) r->spawn[k] = r->spawn_kind[k] = 0u;
  for (k = 0u; k != EG_MAX_ITEM; k++) r->item_cell[k] = r->item_piece[k] = 0u;
  r->start = 0u;

  /* 1-2: the trick, the supporting locks, the bands */
  r->trick = tier == 0u ? (uint8_t)(eg_rnd() & 1u) : (uint8_t)(eg_rnd() & 3u);
  nsup = tier < 2u ? 0u : (tier < 4u ? 1u : 2u);
  r->support = 0u;
  nb = 0u;
  if (r->trick <= TRICK_COLD) band[nb++] = r->trick;
  for (k = 0u; k != nsup; k++) {
    t = (uint8_t)(eg_rnd() & 1u);
    band[nb++] = t;
    r->support |= (uint8_t)(1u << t);
  }
  if (nb == 1u) lk[0] = (uint8_t)(4u + eg_rndn(3u));
  else if (nb == 2u) {
    lk[0] = (uint8_t)(4u + eg_rndn(2u));
    lk[1] = (uint8_t)(8u + eg_rndn(2u));
  } else if (nb == 3u) {
    lk[0] = 3u;
    lk[1] = 6u;
    lk[2] = 9u;
  }
  zt[0] = 1u;
  for (k = 0u; k != nb; k++) {
    zb[k] = (uint8_t)(lk[k] - 1u);
    zt[k + 1u] = (uint8_t)(lk[k] + 1u);
  }
  zb[nb] = EG_H - 2u;
  for (k = 0u; k <= nb; k++) fill_zone(r, k, tier);
  /* the locks: a concrete row with one opening of the obstacle */
  for (k = 0u; k != nb; k++) {
    y = lk[k];
    for (x = 1u; x != EG_W - 1u; x++) c[EG_IDX(x, y)] = T_WALL;
    w = (uint8_t)(2u + eg_rndn(3u));
    a = (uint8_t)(1u + eg_rndn((uint8_t)(15u - w)));
    for (x = a; x != (uint8_t)(a + w); x++) {
      s = EG_IDX(x, y);
      c[s] = band[k] == TRICK_BURN ? T_PAPER : T_WATER;
      /* something to stand on either side of the opening */
      if (c[(uint8_t)(s - EG_W)] == T_WALL) c[(uint8_t)(s - EG_W)] = T_DIRT;
      if (c[(uint8_t)(s + EG_W)] == T_WALL) c[(uint8_t)(s + EG_W)] = T_DIRT;
    }
  }
  settle(r);

  /* the egress door in the north wall; the gate tricks put their shutter under it */
  x = (uint8_t)(2u + eg_rndn(12u));
  r->door = EG_IDX(x, 0u);
  c[r->door] = T_DOOR;
  s = EG_IDX(x, 1u);
  if (r->trick >= TRICK_SHORT) {
    c[s] = r->trick == TRICK_SHORT ? T_DEAD : T_GATE;
    c[(uint8_t)(s - 1u)] = T_WALL;
    c[(uint8_t)(s + 1u)] = T_WALL;
    t = c[(uint8_t)(s + EG_W)];
    if (t == T_WALL || t == T_ROCK) c[(uint8_t)(s + EG_W)] = T_DIRT;
  } else if (c[s] != T_EMPTY)
    c[s] = T_DIRT;

  /* the start, in the bottom band, with no CRT over it */
  for (tries = 0u;; tries++) {
    if (tries == 48u) return 0u;
    s = zone_cell(nb);
    t = c[s];
    if ((t != T_EMPTY && t != T_DIRT) || c[(uint8_t)(s - EG_W)] == T_ROCK || (!nb && EG_Y(s) < 8u)) continue;
    break;
  }
  c[s] = T_EMPTY;
  r->start = s;

  /* the gate's apparatus */
  if (r->trick == TRICK_WEIGHT) {
    /* a plate in a niche in the north wall: no CRT climbs or is pushed into it, so the weight must be made */
    for (tries = 0u;; tries++) {
      if (tries == 40u) return 0u;
      xn = (uint8_t)(1u + eg_rndn(14u));
      if (absd(xn, EG_X(r->door)) < 2u) continue;
      break;
    }
    c[EG_IDX(xn, 0u)] = T_PLATE;
    c[EG_IDX(xn, 1u)] = T_DIRT;
  } else if (r->trick == TRICK_SHORT) {
    /* a dead socket in a side wall, a pool of water against it; the pool is the only way to reach it */
    k = nb ? (uint8_t)(1u + eg_rndn(nb)) : 0u;
    for (tries = 0u;; tries++) {
      if (tries == 40u) return 0u;
      y = (uint8_t)(zt[k] + eg_rndn((uint8_t)(zb[k] - zt[k] + 1u)));
      if (y < 2u) continue;
      break;
    }
    n = (uint8_t)(2u + eg_rndn(2u));
    a = (uint8_t)(eg_rnd() & 1u); /* 0 west, 1 east */
    c[EG_IDX(a ? EG_W - 1u : 0u, y)] = T_SOCKET;
    for (i = 1u; i <= n; i++) c[EG_IDX(a ? EG_W - 1u - i : i, y)] = T_WATER;
    s = EG_IDX(a ? EG_W - 2u - n : n + 1u, y);
    if (c[s] == T_WALL || c[s] == T_ROCK) c[s] = T_DIRT;
  }
  settle(r);
  if (c[r->start] != T_EMPTY) return 0u;

  /* 3: the objects and their ingredients, from the goal outwards */
  nneed = 0u;
  depth = 0u;
  d = tier == 0u ? 1u : (tier < 2u ? 1u : (tier < 4u ? 2u : 3u));
  /* the supporting locks first, at their simplest; the named trick then goes as deep as the budget allows. The
   * gate's ingredients may lie anywhere before the goal band, a lock's only below it. */
  a = r->trick <= TRICK_COLD ? 1u : 0u; /* band 0 is the named trick's own lock */
  for (k = a; k != nb; k++)
    if (!add_lock(band[k], 1u, (uint8_t)(k + 1u), &depth)) return 0u;
  if (a ? !add_lock(r->trick, d, 1u, &depth) : !add_lock(r->trick, d, nb ? 1u : 0u, &depth)) return 0u;
  /* 4: red herrings, if the pocket has room. A herring must never soft-lock the room (3.1): when its recipe with
   * a piece the room needs would use that piece up, the room re-supplies it (a second one lies in the same band);
   * a herring that would need more than the pocket holds is dealt again. HOT stays HOT (FIRE + EARTH = LAVA still
   * burns paper), so that misuse is harmless. */
  herr = tier == 0u ? 0u : (tier < 3u ? 1u : 2u);
  r->herrings = 0u;
  nh = 0u;
  while (herr--) {
    for (tries = 0u; tries != 8u; tries++) {
      static const uint8_t pool[5] = {P_FIRE, P_WATER, P_AIR, P_EARTH, P_METAL};
      uint8_t h = pool[eg_rndn(5u)], dup = 0u, extra[EG_MAX_ITEM];
      for (k = 0u; k != nneed; k++) {
        t = eg_recipe(h, need[k]);
        if (!t) continue;
        if (needop[k] && (eg_piece_tags(t) & eg_piece_tags(need[k])) == eg_piece_tags(need[k])) continue;
        for (i = 0u; i != dup; i++)
          if (extra[i] == k) break;
        if (i == dup && dup < EG_MAX_ITEM) extra[dup++] = k;
      }
      if (nneed + nh + 1u + dup > EG_MAX_ITEM) continue;
      for (i = 0u; i != dup; i++) {
        need[nneed] = need[extra[i]];
        needz[nneed] = needz[extra[i]];
        needop[nneed] = needop[extra[i]];
        nneed++;
      }
      hpiece[nh++] = h;
      break;
    }
  }
  for (k = 0u; k != nh; k++) {
    need[nneed] = hpiece[k];
    needz[nneed] = nb ? 1u : 0u;
    needop[nneed] = 0u;
    nneed++;
    r->herrings++;
  }
  r->depth = depth;
  for (k = 0u; k != nneed; k++) {
    i = free_cell(r, needz[k], nb, 2u);
    if (!i) return 0u;
    c[i] = T_ITEM;
    r->item_cell[r->nitem] = i;
    r->item_piece[r->nitem] = need[k];
    r->nitem++;
  }

  /* 5: the keycard behind the last lock (a gate room without locks: far from the start); tier 4 up a second */
  n = tier >= 4u ? 2u : 1u;
  for (k = 0u; k != n; k++) {
    i = k == 0u ? free_cell(r, 0u, 0u, nb ? 0u : 6u) : free_cell(r, nb > 1u ? 1u : 0u, nb > 1u ? 1u : 0u, 4u);
    if (!i) return 0u;
    c[i] = T_KEY;
    r->keys++;
  }
  /* the physical layer around what the plan needs: more of it, and the nastier kinds, deeper down */
  n = tier < 2u ? 2u : 4u;
  for (k = 0u; k != r->nitem; k++)
    if (eg_rndn(10u) < (uint8_t)(3u + tier)) guard(r, r->item_cell[k], eg_rndn(n));
  for (i = EG_W; i != (uint8_t)(EG_N - EG_W); i++)
    if (c[i] == T_KEY && eg_rndn(10u) < (uint8_t)(3u + tier)) guard(r, i, eg_rndn(n));
  /* tokens: a few, as the reward for a side pocket (a cell boxed in on three sides), else anywhere off the start */
  n = (uint8_t)(3u + (tier >> 1));
  for (tries = 0u; r->tokens != n && tries != 120u; tries++) {
    i = free_cell(r, 0u, nb, 3u);
    if (!i) break;
    for (k = 0u, d = 0u; d != 4u; d++)
      if (!open_cell(c[(uint8_t)(i + step4[d])])) k++;
    if (k < 3u && tries < 90u) continue;
    c[i] = T_TOKEN;
    r->tokens++;
  }
  settle(r);
  if (c[r->start] != T_EMPTY) return 0u;
  /* enemies: mites walk open floor, eyes float anywhere that is not concrete */
  n = (uint8_t)(2u + tier);
  if (n > EG_MAX_SPAWN) n = EG_MAX_SPAWN;
  for (tries = 0u; r->nspawn != n && tries != 160u; tries++) {
    i = EG_IDX(1u + eg_rndn(14u), 1u + eg_rndn(12u));
    t = c[i];
    k = (r->nspawn & 1u) ? SPAWN_EYE : SPAWN_MITE;
    if (k == SPAWN_MITE && tries > 100u) k = SPAWN_EYE;
    if (dist(i, r->start) < 6u) continue;
    if (k == SPAWN_MITE ? t != T_EMPTY : (t == T_WALL || t == T_ROCK)) continue;
    for (d = 0u; d != r->nspawn; d++)
      if (dist(i, r->spawn[d]) < 3u) break;
    if (d != r->nspawn) continue;
    r->spawn[r->nspawn] = i;
    r->spawn_kind[r->nspawn] = k;
    r->nspawn++;
  }
  settle(r);
  return (uint8_t)(c[r->start] == T_EMPTY);
}
