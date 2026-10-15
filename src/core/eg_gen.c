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
static uint8_t nb, lk[3], th[3], zt[4], zb[4], band[3];
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

static void protect(eg_room *r, uint8_t i) { r->prot[i >> 3] |= (uint8_t)(1u << (i & 7u)); }

static uint8_t lock_obj(uint8_t trick, uint8_t deep) {
  if (trick == TRICK_BURN) return P_FIRE;
  if (trick == TRICK_COLD) return deep >= 3u ? P_ICE : P_SNOW;
  return trick == TRICK_SHORT ? P_ELECTRIC : P_BRICK;
}

/* queue a lock's ingredients: as deep as `want` allows within the item budget */
static uint8_t add_lock(uint8_t trick, uint8_t want, uint8_t minz, uint8_t *depth) {
  uint8_t buf[5], n, d = want, k;
  if (trick >= TRICK_GLASS) return 1u; /* crossed, not opened: nothing to make */
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

/* --- B2 part B: readable wall structures. The concrete inside a room comes from a few templates, each one connected
 * piece: a line or an L (anchored on a side wall), a U, a ring with a gap, a counter with a pass-through. A placement
 * that would cut the band in two is undone, so a structure steers a path and never seals one. Protected cells are
 * left alone (the generator reserves routes and openings with them). --- */
static uint8_t shx[18], shy[18], shn, shw, shh;
static void sh_add(uint8_t x, uint8_t y) {
  shx[shn] = x;
  shy[shn] = y;
  shn++;
  if ((uint8_t)(x + 1u) > shw) shw = (uint8_t)(x + 1u);
  if ((uint8_t)(y + 1u) > shh) shh = (uint8_t)(y + 1u);
}

static void shape(uint8_t kind) {
  uint8_t a, b, i, g;
  shn = shw = shh = 0u;
  switch (kind) {
    case ST_LINE:
      a = (uint8_t)(3u + eg_rndn(3u));
      for (i = 0u; i != a; i++) sh_add(i, 0u);
      break;
    case ST_L:
      a = (uint8_t)(3u + eg_rndn(2u));
      b = (uint8_t)(2u + eg_rndn(2u));
      for (i = 0u; i != a; i++) sh_add(i, 0u);
      for (i = 1u; i != b; i++) sh_add(0u, i);
      break;
    case ST_U:
      a = (uint8_t)(3u + eg_rndn(2u));
      b = (uint8_t)(2u + eg_rndn(2u));
      for (i = 0u; i != a; i++) sh_add(i, (uint8_t)(b - 1u));
      for (i = 0u; i != (uint8_t)(b - 1u); i++) {
        sh_add(0u, i);
        sh_add((uint8_t)(a - 1u), i);
      }
      break;
    case ST_RING: /* the gap: one cell in the middle of a side */
      a = (uint8_t)(4u + eg_rndn(2u));
      b = (uint8_t)(3u + eg_rndn(2u));
      g = eg_rndn(4u);
      for (i = 0u; i != a; i++) {
        if (!(g == 0u && i == (uint8_t)(a >> 1))) sh_add(i, 0u);
        if (!(g == 1u && i == (uint8_t)(a >> 1))) sh_add(i, (uint8_t)(b - 1u));
      }
      for (i = 1u; i != (uint8_t)(b - 1u); i++) {
        if (!(g == 2u && i == (uint8_t)(b >> 1))) sh_add(0u, i);
        if (!(g == 3u && i == (uint8_t)(b >> 1))) sh_add((uint8_t)(a - 1u), i);
      }
      break;
    default: /* ST_COUNTER: a pass-through near the middle */
      a = (uint8_t)(5u + eg_rndn(4u));
      g = (uint8_t)((a >> 1) - eg_rndn(2u));
      for (i = 0u; i != a; i++)
        if (i != g) sh_add(i, 0u);
      break;
  }
}

/* every open cell of rows y0..y1 reaches every other inside them (only concrete blocks) */
static uint8_t band_whole(const eg_room *r, uint8_t y0, uint8_t y1) {
  static uint8_t q[EG_N], seen[EG_N];
  uint8_t i, n = 0u, h = 0u, open = 0u, c, d, s;
  for (i = 0u; i != (uint8_t)EG_N; i++) seen[i] = 0u;
  for (i = EG_IDX(1u, y0); i <= EG_IDX(EG_W - 2u, y1); i++) {
    if (EG_X(i) == 0u || EG_X(i) == EG_W - 1u || (r->c[i] & T_MASK) == T_WALL) continue;
    open++;
    if (!n) {
      q[n++] = i;
      seen[i] = 1u;
    }
  }
  while (h != n) {
    c = q[h++];
    for (d = 0u; d != 4u; d++) {
      s = (uint8_t)(c + step4[d]);
      if (seen[s] || EG_Y(s) < y0 || EG_Y(s) > y1 || EG_X(s) == 0u || EG_X(s) == EG_W - 1u ||
          (r->c[s] & T_MASK) == T_WALL)
        continue;
      seen[s] = 1u;
      q[n++] = s;
    }
  }
  return (uint8_t)(n == open);
}

uint8_t eg_structure(eg_room *r, uint8_t y0, uint8_t y1, uint8_t kind) EG_BANKED {
  uint8_t tries, k, t, x, y, ox, oy, w, h, tmp, cells[18];
  for (tries = 0u; tries != 12u; tries++) {
    shape(kind);
    t = eg_rnd();
    w = shw;
    h = shh;
    if (t & 1u) w = shh, h = shw; /* transposed */
    if (w > EG_W - 2u || h > (uint8_t)(y1 - y0 + 1u)) continue;
    /* lines and Ls lean on a side wall; the rest stand free */
    if (kind <= ST_L && (t & 8u))
      ox = (t & 16u) ? 1u : (uint8_t)(EG_W - 1u - w);
    else
      ox = (uint8_t)(1u + eg_rndn((uint8_t)(EG_W - 1u - w)));
    oy = (uint8_t)(y0 + eg_rndn((uint8_t)(y1 - y0 + 2u - h)));
    for (k = 0u; k != shn; k++) {
      x = shx[k];
      y = shy[k];
      if (t & 1u) tmp = x, x = y, y = tmp;
      if (t & 2u) x = (uint8_t)(w - 1u - x);
      if (t & 4u) y = (uint8_t)(h - 1u - y);
      cells[k] = EG_IDX(ox + x, oy + y);
      if (r->c[cells[k]] != T_EMPTY || EG_PROT(r, cells[k])) break;
    }
    if (k != shn) continue;
    for (k = 0u; k != shn; k++) r->c[cells[k]] = T_WALL;
    if (band_whole(r, y0, y1)) return 1u;
    for (k = 0u; k != shn; k++) r->c[cells[k]] = T_EMPTY;
  }
  return 0u;
}

/* a band reads floor first: open carpet, a few wall structures (not in the goal band), and insulation foam in a
 * couple of deliberate patches. CRTs and drywall are not scattered: guard() places them where they matter. */
static void fill_zone(eg_room *r, uint8_t z, uint8_t tier) {
  uint8_t *c = r->c;
  uint8_t k, n, len, s, t, x, y, h = (uint8_t)(zb[z] - zt[z] + 1u);
  for (y = zt[z]; y <= zb[z]; y++)
    for (x = 1u; x != EG_W - 1u; x++) c[EG_IDX(x, y)] = T_EMPTY;
  if (h >= 3u && z) { /* no concrete in the goal band: the door, the plate niche and the keycard stay open */
    /* not in the rows against a lock: its opening clears a cell on each side, which would cut a structure */
    n = (uint8_t)(1u + (tier >> 2) + (h >= 6u));
    for (k = 0u; k != n; k++) eg_structure(r, (uint8_t)(zt[z] + 1u), (uint8_t)(z < nb ? zb[z] - 1u : zb[z]), eg_rndn(ST_KINDS));
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
    /* the drywall goes on one of the sides that can take it (else the alcove would be sealed) */
    side = (uint8_t)(eg_rnd() & 3u);
    for (d = 0u; d != 4u; d++, side = (uint8_t)((side + 1u) & 3u)) {
      s = (uint8_t)(i + step4[side]);
      if (open_cell(c[s]) && s != r->start && EG_Y(s) != 0u && EG_Y(s) != EG_H - 1u) break;
    }
    if (d == 4u) return;
    for (d = 0u; d != 4u; d++) {
      s = (uint8_t)(i + step4[d]);
      if (!open_cell(c[s]) || s == r->start || EG_Y(s) == 0u || EG_Y(s) == EG_H - 1u) continue;
      c[s] = d == side ? T_BRICK : T_WALL;
    }
    /* B2 part B: the corners close it into one structure, a cubicle with a drywall panel for its way in */
    for (d = 0u; d != 4u; d++) {
      static const int8_t corner[4] = {-17, -15, 15, 17}; /* NW NE SW SE */
      s = (uint8_t)(i + corner[d]);
      if (c[s] == T_EMPTY && s != r->start && !EG_PROT(r, s) && EG_X(s) && EG_X(s) != EG_W - 1u && EG_Y(s) &&
          EG_Y(s) != EG_H - 1u)
        c[s] = T_WALL;
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
  eg_plates_dirty = 1u; /* a new room: the shutter pass syncs its gates */
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
  r->trick = eg_rndn(TRICK_COUNT);
  for (k = 0u; k != (uint8_t)(EG_N / 8u); k++) r->prot[k] = 0u;
  r->nob = r->nrun = 0u;
  for (k = 0u; k != EG_MAX_OB; k++)
    for (n = 0u; n != 4u; n++) r->ob[k][n] = 0u;
  for (k = 0u; k != EG_MAX_RUN; k++) r->run[k] = 0u;
  if (TRICK_OBSTACLE(r->trick)) { /* B2: the living-obstacle rooms are built in eg_ogen.c */
    r->support = 0u;
    return eg_gen_obstacles(r, tier);
  }
  nsup = tier < 2u ? 0u : (tier < 4u ? 1u : 2u);
  r->support = 0u;
  nb = 0u;
  if (r->trick <= TRICK_COLD || r->trick >= TRICK_GLASS) band[nb++] = r->trick;
  for (k = 0u; k != nsup; k++) {
    t = (uint8_t)(eg_rnd() & 1u);
    band[nb++] = t;
    r->support |= (uint8_t)(1u << t);
  }
  /* a lock is one row, the glitched-glass band 2 (3 from tier 3, when there is room) */
  for (k = 0u; k != 3u; k++) th[k] = 1u;
  if (r->trick == TRICK_RHYTHM) th[0] = (uint8_t)(tier >= 3u && nb < 3u ? 3u : 2u);
  if (nb == 1u) lk[0] = (uint8_t)(4u + eg_rndn(3u));
  else if (nb == 2u) {
    lk[0] = (uint8_t)(4u + eg_rndn(2u));
    lk[1] = (uint8_t)(8u + eg_rndn(2u) + th[0] - 1u);
  } else if (nb == 3u) {
    lk[0] = 3u;
    lk[1] = (uint8_t)(6u + th[0] - 1u);
    lk[2] = (uint8_t)(9u + th[0] - 1u);
  }
  zt[0] = 1u;
  for (k = 0u; k != nb; k++) {
    zb[k] = (uint8_t)(lk[k] - 1u);
    zt[k + 1u] = (uint8_t)(lk[k] + th[k]);
  }
  zb[nb] = EG_H - 2u;
  for (k = 0u; k <= nb; k++) fill_zone(r, k, tier);
  /* the locks: a concrete row with one opening of the obstacle; the fragile locks are the void (pits) bridged by
   * glass (1-2 cells: one crossing each) or by a column of glitched glass */
  for (k = 0u; k != nb; k++) {
    y = lk[k];
    t = band[k] >= TRICK_GLASS ? T_PIT : T_WALL;
    for (i = 0u; i != th[k]; i++)
      for (x = 1u; x != EG_W - 1u; x++) c[EG_IDX(x, y + i)] = t;
    w = band[k] == TRICK_RHYTHM ? 1u : (band[k] == TRICK_GLASS ? (uint8_t)(1u + (tier < 2u)) : (uint8_t)(2u + eg_rndn(3u)));
    a = (uint8_t)(2u + eg_rndn((uint8_t)(13u - w)));
    for (x = a; x != (uint8_t)(a + w); x++) {
      for (i = 0u; i != th[k]; i++)
        c[EG_IDX(x, y + i)] = band[k] == TRICK_BURN ? T_PAPER
                              : band[k] == TRICK_GLASS ? T_GLASS
                              : band[k] == TRICK_RHYTHM ? T_GLITCH : T_WATER;
      /* something to stand on either side of the opening (kept: protected) */
      s = EG_IDX(x, y - 1u);
      if (c[s] == T_WALL || band[k] >= TRICK_GLASS) c[s] = T_EMPTY;
      protect(r, s);
      s = EG_IDX(x, y + th[k]);
      if (c[s] == T_WALL || band[k] >= TRICK_GLASS) c[s] = T_EMPTY;
      protect(r, s);
    }
  }
  settle(r);

  /* the egress door in the north wall; the gate tricks put their shutter under it */
  x = (uint8_t)(2u + eg_rndn(12u));
  r->door = EG_IDX(x, 0u);
  c[r->door] = T_DOOR;
  s = EG_IDX(x, 1u);
  if (r->trick == TRICK_SHORT || r->trick == TRICK_WEIGHT) {
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
    if (r->trick == TRICK_GLASS) /* one-way glass: the keycard lies below the bridge (take it before you cross) */
      i = free_cell(r, 1u, nb, 3u);
    else
      i = k == 0u ? free_cell(r, 0u, 0u, nb ? 0u : 6u) : free_cell(r, nb > 1u ? 1u : 0u, nb > 1u ? 1u : 0u, 4u);
    if (!i) return 0u;
    c[i] = T_KEY;
    r->keys++;
  }
  /* the physical layer around what the plan needs: more of it, and the nastier kinds, deeper down */
  n = tier < 2u ? 2u : 4u;
  /* over the void no CRT may stand anywhere (one would fill a pit: a way round the bridge): alcoves and foam only */
  a = (uint8_t)(r->trick >= TRICK_GLASS);
  for (k = 0u; k != r->nitem; k++)
    if (eg_rndn(10u) < (uint8_t)(3u + tier)) guard(r, r->item_cell[k], a ? (uint8_t)(1u + ((eg_rnd() & 1u) << 1)) : eg_rndn(n));
  for (i = EG_W; i != (uint8_t)(EG_N - EG_W); i++)
    if (c[i] == T_KEY && eg_rndn(10u) < (uint8_t)(3u + tier))
      guard(r, i, a ? (uint8_t)(1u + ((eg_rnd() & 1u) << 1)) : eg_rndn(n));
  /* protected: what the plan stands on or needs (never fragile; no mob may break it, B2) */
  protect(r, r->start);
  for (d = 0u; d != 4u; d++) protect(r, (uint8_t)(r->start + step4[d]));
  protect(r, (uint8_t)(r->door + EG_W));
  protect(r, (uint8_t)(r->door + 2u * EG_W));
  for (k = 0u; k != r->nitem; k++) protect(r, r->item_cell[k]);
  for (i = 0u; i != (uint8_t)EG_N; i++) {
    t = c[i];
    if (t == T_KEY || t == T_SOCKET || t == T_PLATE || t == T_GATE || t == T_DEAD) protect(r, i);
    if (t == T_PLATE) protect(r, (uint8_t)(i + EG_W));
    if (t == T_WATER && r->trick == TRICK_SHORT && (EG_X(i) <= 4u || EG_X(i) >= EG_W - 5u)) protect(r, i);
  }
  /* step routing (tier 1 up): a concrete run gets a gap of worn carpet (3 crossings) or, deeper, ceiling tile (2);
   * and a few worn tiles lie in the open where a careless path wears them through */
  if (tier) {
    n = (uint8_t)(1u + (tier >= 3u));
    for (tries = 0u; n && tries != 60u; tries++) {
      i = EG_IDX(2u + eg_rndn(12u), 2u + eg_rndn(10u));
      for (k = 0u; k != nb; k++)
        if (EG_Y(i) >= lk[k] && EG_Y(i) < (uint8_t)(lk[k] + th[k])) break;
      if (k != nb || c[i] != T_WALL || EG_PROT(r, i) || c[(uint8_t)(i - 1u)] != T_WALL || c[(uint8_t)(i + 1u)] != T_WALL ||
          c[(uint8_t)(i - 2u)] != T_WALL || c[(uint8_t)(i + 2u)] != T_WALL || /* both halves stay structures */
          !open_cell(c[(uint8_t)(i - EG_W)]) || !open_cell(c[(uint8_t)(i + EG_W)]))
        continue;
      c[i] = tier >= 2u && (eg_rnd() & 1u) ? T_CEIL : T_WORN;
      n--;
    }
    n = (uint8_t)(1u + eg_rndn(3u));
    for (tries = 0u; n && tries != 60u; tries++) {
      i = free_cell(r, nb ? 1u : 0u, nb, 3u);
      if (!i || c[i] != T_EMPTY || EG_PROT(r, i) || c[(uint8_t)(i - EG_W)] == T_ROCK) continue;
      c[i] = T_WORN;
      n--;
    }
  }
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
