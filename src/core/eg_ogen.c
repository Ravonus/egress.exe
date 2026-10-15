/* EGRESS room generator, B2: the living-obstacle rooms (docs/redesign.md 7.7). eg_generate (eg_gen.c) deals the
 * trick; the four obstacle tricks are built here:
 *
 *   RUSH    rush hour: a counter (a wall row) with two pass-throughs, and a COMMUTER's loop through both. The door
 *           is above it, you start below: cross when the loop lets you (two out of phase from tier 2).
 *   STEER   steer the heavy: the way up is a glass partition only a MOVER breaks. It wanders the hall below, turning
 *           at walls; push the crate where it turns it into the partition. From tier 2 it may be a STANDBY asleep by
 *           floor glass: break the glass to wake it. From tier 4 a COMMUTER walks the door's band.
 *   MIRROR  the door's shutter needs every plate weighted. One plate is yours; the other lies in a sealed cubicle
 *           with a crate and the OTHER TEMP, which copies your steps flipped on its axis: push both at once.
 *   STARE   a COAT RACK stands on its plinth in the only doorway up. It moves one cell toward you on every tick you
 *           don't look at it (you start looking). Draw it out of the doorway and get round it.
 *
 * Obstacle rooms hold no foam, CRTs, drywall, pieces, tokens or enemies: the tick solver (host/eg_osolve.c) models
 * every cell they have, and the cartridge plays exactly that. The walls come from the B2 structure templates
 * (eg_structure). The host proves every room fair before the cartridge plays it (tools/bake_seeds.sh). */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_obst.h"
#include "eg_room.h"

static const int8_t st4[4] = {-16, 16, -1, 1};
static uint8_t lk; /* the lock row */

static void prot(eg_room *r, uint8_t i) { r->prot[i >> 3] |= (uint8_t)(1u << (i & 7u)); }
static uint8_t absd(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }

/* a carpet cell in rows y0..y1 that isn't protected, `far` or more from `from` (255: anywhere); 0 if none found */
static uint8_t open_at(eg_room *r, uint8_t y0, uint8_t y1, uint8_t from, uint8_t far) {
  uint8_t tries, i;
  for (tries = 0u; tries != 60u; tries++) {
    i = EG_IDX(1u + eg_rndn(14u), y0 + eg_rndn((uint8_t)(y1 - y0 + 1u)));
    if (r->c[i] != T_EMPTY || EG_PROT(r, i)) continue;
    if (from != 255u && (uint8_t)(absd(EG_X(i), EG_X(from)) + absd(EG_Y(i), EG_Y(from))) < far) continue;
    return i;
  }
  return 0u;
}

/* open on all four sides: a crate there can be pushed every way */
static uint8_t free4(const eg_room *r, uint8_t i) {
  uint8_t d;
  for (d = 0u; d != 4u; d++)
    if (r->c[(uint8_t)(i + st4[d])] != T_EMPTY) return 0u;
  return 1u;
}

static void add_ob(eg_room *r, uint8_t a, uint8_t cell, uint8_t b, uint8_t p) {
  uint8_t *o = r->ob[r->nob++];
  o[0] = a;
  o[1] = cell;
  o[2] = b;
  o[3] = p;
}

/* the door in the north wall, its threshold kept clear */
static void door(eg_room *r) {
  uint8_t x = (uint8_t)(2u + eg_rndn(12u));
  r->door = EG_IDX(x, 0u);
  r->c[r->door] = T_DOOR;
  prot(r, EG_IDX(x, 1u));
  if (lk > 2u) prot(r, EG_IDX(x, 2u));
}

/* a lock row: concrete across, one or two openings (kept open: both sides protected) */
static void lock_row(eg_room *r, uint8_t xa, uint8_t xb) {
  uint8_t x;
  for (x = 1u; x != EG_W - 1u; x++) r->c[EG_IDX(x, lk)] = T_WALL;
  for (x = 0u; x != 2u; x++) {
    uint8_t g = x ? xb : xa;
    if (!g) continue;
    r->c[EG_IDX(g, lk)] = T_EMPTY;
    prot(r, EG_IDX(g, lk));
    prot(r, EG_IDX(g, lk - 1u));
    prot(r, EG_IDX(g, lk + 1u));
  }
}

static uint8_t key_at(eg_room *r, uint8_t y0, uint8_t y1, uint8_t from, uint8_t far) {
  uint8_t i = open_at(r, y0, y1, from, far);
  if (!i) return 0u;
  r->c[i] = T_KEY;
  prot(r, i);
  r->keys++;
  return 1u;
}

/* the start low in the start band; the keycards: the first in the door's band away from the door, a second (tier 4
 * up) below, away from the start */
static uint8_t keys_and_start(eg_room *r, uint8_t tier, uint8_t start_y0) {
  uint8_t i = open_at(r, start_y0, EG_H - 2u, 255u, 0u);
  if (!i) return 0u;
  r->start = i;
  prot(r, i);
  if (!key_at(r, 1u, (uint8_t)(lk - 1u), r->door, 3u)) return 0u;
  return (uint8_t)(tier < 4u || key_at(r, (uint8_t)(lk + 1u), EG_H - 2u, r->start, 5u));
}

/* a few fragile cells in the start band, off the protected ones (stepping off them is a choice) */
static void fragile(eg_room *r, uint8_t n, uint8_t t) {
  uint8_t i;
  while (n--) {
    i = open_at(r, (uint8_t)(lk + 1u), EG_H - 2u, r->start, 3u);
    if (i) r->c[i] = t;
  }
}

static uint8_t rush(eg_room *r, uint8_t tier) {
  uint8_t xa, xb, y1, y2, k, n, L, x, y;
  lk = (uint8_t)(4u + eg_rndn(3u));
  xa = (uint8_t)(2u + eg_rndn(7u));
  xb = (uint8_t)(xa + 3u + eg_rndn(4u));
  if (xb > EG_W - 3u) xb = EG_W - 3u;
  y1 = (uint8_t)(lk - 1u - eg_rndn(lk > 3u ? 2u : 1u));
  y2 = (uint8_t)(lk + 2u + eg_rndn(2u));
  lock_row(r, xa, xb);
  /* the loop: up the west pass-through, east above the counter, down the east one, west below it */
  r->nrun = 4u;
  r->run[0] = (uint8_t)((0u << 6) | (uint8_t)(y2 - y1));
  r->run[1] = (uint8_t)((1u << 6) | (uint8_t)(xb - xa));
  r->run[2] = (uint8_t)((2u << 6) | (uint8_t)(y2 - y1));
  r->run[3] = (uint8_t)((3u << 6) | (uint8_t)(xb - xa));
  for (y = y1; y <= y2; y++) {
    prot(r, EG_IDX(xa, y));
    prot(r, EG_IDX(xb, y));
  }
  for (x = xa; x <= xb; x++) {
    prot(r, EG_IDX(x, y1));
    prot(r, EG_IDX(x, y2));
  }
  L = eg_route_len(r);
  n = tier >= 2u ? 2u : 1u;
  for (k = 0u; k != n; k++) {
    uint8_t pos = (uint8_t)(k ? L >> 1 : 0u), c = EG_IDX(xa, y2), s;
    for (s = 0u; s != pos; s++) { /* walk the route to its start */
      uint8_t j, q = s, d = 0u;
      for (j = 0u; j != r->nrun; j++) {
        if (q < (r->run[j] & 63u)) {
          d = (uint8_t)(r->run[j] >> 6);
          break;
        }
        q = (uint8_t)(q - (r->run[j] & 63u));
      }
      c = (uint8_t)(c + (d == 0u ? -16 : d == 1u ? 1 : d == 2u ? 16 : -1));
    }
    add_ob(r, OB_PATROL, c, 0u, pos);
  }
  door(r);
  for (k = 0u; k != (uint8_t)(1u + (tier >> 1)); k++) eg_structure(r, (uint8_t)(lk + 1u), EG_H - 2u, eg_rndn(ST_KINDS));
  if (!keys_and_start(r, tier, (uint8_t)(y2 + 1u > EG_H - 3u ? EG_H - 3u : y2 + 1u))) return 0u;
  if (tier) fragile(r, (uint8_t)(1u + (tier >= 3u)), tier >= 3u ? T_GLASS : T_WORN);
  return 1u;
}

static uint8_t steer(eg_room *r, uint8_t tier) {
  uint8_t xg, i, k, a, b, n;
  lk = (uint8_t)(3u + eg_rndn(3u));
  xg = (uint8_t)(2u + eg_rndn(12u));
  lock_row(r, xg, 0u);
  r->c[EG_IDX(xg, lk)] = T_PARTITION;
  door(r);
  for (k = 0u; k != (uint8_t)(1u + (tier >= 3u)); k++)
    eg_structure(r, (uint8_t)(lk + 2u), EG_H - 2u, eg_rndn(ST_KINDS));
  if (!keys_and_start(r, tier, EG_H - 3u)) return 0u;
  /* the MOVER (or a STANDBY: tier 2 up), in the hall below, away from the start */
  i = open_at(r, (uint8_t)(lk + 1u), EG_H - 2u, r->start, 4u);
  if (!i) return 0u;
  a = (uint8_t)(OBA_P2 | ((eg_rnd() & 1u) ? OBA_LEFT : 0u));
  b = eg_rndn(4u);
  if (tier >= 2u && (eg_rnd() & 1u)) {
    add_ob(r, (uint8_t)(OB_SLEEPER | a | OBA_LOCK), i, (uint8_t)(b | (1u << 2) | OBB_ASLEEP),
           (uint8_t)(3u | ((eg_rnd() & 1u) ? OBP_PHASE : 0u)));
    /* the alarm: floor glass within its wake radius */
    for (k = 0u, n = 0u; k != 30u && n != 2u; k++) {
      uint8_t j = EG_IDX(EG_X(i) + eg_rndn(7u) - 3u, EG_Y(i) + eg_rndn(7u) - 3u);
      if (EG_Y(j) <= lk || EG_Y(j) >= EG_H - 1u || !EG_X(j) || EG_X(j) >= EG_W - 1u || r->c[j] != T_EMPTY ||
          EG_PROT(r, j) || j == i)
        continue;
      r->c[j] = T_GLASS;
      n++;
    }
    if (!n) return 0u;
  } else
    add_ob(r, (uint8_t)(OB_HEAVY | a), i, b, (eg_rnd() & 1u) ? OBP_PHASE : 0u);
  prot(r, i);
  /* the crate(s): pushable every way where they start */
  n = tier >= 3u ? 2u : 1u;
  for (k = 0u; k != 40u && n; k++) {
    uint8_t j = open_at(r, (uint8_t)(lk + 2u), EG_H - 2u, r->start, 2u);
    if (!j || !free4(r, j)) continue;
    r->c[j] = T_BLOCK;
    n--;
  }
  if (n) return 0u;
  if (tier && OBA_RULE(r->ob[0][0]) != OB_SLEEPER) fragile(r, (uint8_t)(tier >= 3u ? 2u : 1u), T_GLASS);
  /* support (tier 4 up): a COMMUTER loops a short route in the door's band */
  if (tier >= 4u && lk >= 4u) {
    a = (uint8_t)(1u + eg_rndn(9u));
    b = (uint8_t)(a + 3u);
    for (i = a; i <= b; i++)
      if (r->c[EG_IDX(i, 1u)] == T_KEY || r->c[EG_IDX(i, 2u)] == T_KEY) return 0u;
    r->nrun = 4u;
    r->run[0] = (1u << 6) | 3u;
    r->run[1] = (2u << 6) | 1u;
    r->run[2] = (3u << 6) | 3u;
    r->run[3] = (0u << 6) | 1u;
    add_ob(r, OB_PATROL, EG_IDX(a, 1u), 0u, 0u);
  }
  return 1u;
}

static uint8_t mirror(eg_room *r, uint8_t tier) {
  uint8_t x, y, w, h, ox, oy, i, k, xd;
  lk = 1u;
  door(r);
  xd = EG_X(r->door);
  r->c[EG_IDX(xd, 1u)] = T_GATE;
  r->c[EG_IDX(xd - 1u, 1u)] = T_WALL;
  r->c[EG_IDX(xd + 1u, 1u)] = T_WALL;
  prot(r, EG_IDX(xd, 2u));
  /* the sealed cubicle: a ring of concrete round a 3x3 (or 4x3) floor */
  w = (uint8_t)(3u + (tier >= 3u && (eg_rnd() & 1u)));
  h = 3u;
  ox = (uint8_t)(1u + eg_rndn((uint8_t)(EG_W - 3u - w)));
  oy = (uint8_t)(3u + eg_rndn((uint8_t)(EG_H - 5u - h - 2u)));
  for (y = 0u; y != (uint8_t)(h + 2u); y++)
    for (x = 0u; x != (uint8_t)(w + 2u); x++) {
      i = EG_IDX(ox + x, oy + y);
      r->c[i] = (x == 0u || y == 0u || x == w + 1u || y == h + 1u) ? T_WALL : T_EMPTY;
      prot(r, i);
    }
  /* inside: the crate in the middle, the plate on an edge, the mirror on another edge */
  {
    uint8_t cx = (uint8_t)(ox + 1u + (w >> 1)), cy = (uint8_t)(oy + 2u), e = eg_rndn(4u), f;
    r->c[EG_IDX(cx, cy)] = T_BLOCK;
    i = (uint8_t)(EG_IDX(cx, cy) + st4[e]);
    r->c[i] = T_PLATE;
    f = (uint8_t)((e + 1u + eg_rndn(3u)) & 3u);
    i = (uint8_t)(EG_IDX(cx, cy) + st4[f]);
    if (r->c[i] != T_EMPTY) return 0u;
    add_ob(r, OB_MIRROR, i, (uint8_t)((1u + eg_rndn(3u)) << 2), 0u);
  }
  if (tier >= 2u) eg_structure(r, 3u, EG_H - 2u, eg_rndn(ST_KINDS));
  /* yours: a crate free on every side, the plate two cells from it in a line */
  for (k = 0u; k != 60u; k++) {
    uint8_t d = eg_rndn(4u), p2;
    i = open_at(r, 3u, EG_H - 3u, 255u, 0u);
    if (!i || !free4(r, i)) continue;
    p2 = (uint8_t)(i + st4[d] + st4[d]);
    if (EG_X(p2) == 0u || EG_X(p2) >= EG_W - 1u || EG_Y(p2) == 0u || EG_Y(p2) >= EG_H - 1u || r->c[p2] != T_EMPTY ||
        EG_PROT(r, p2) || r->c[(uint8_t)(i + st4[d])] != T_EMPTY)
      continue;
    r->c[i] = T_BLOCK;
    r->c[p2] = T_PLATE;
    prot(r, p2);
    break;
  }
  if (k == 60u) return 0u;
  lk = 3u; /* the keycards: anywhere below the door's row */
  i = open_at(r, EG_H - 3u, EG_H - 2u, 255u, 0u);
  if (!i) return 0u;
  r->start = i;
  prot(r, i);
  return (uint8_t)(key_at(r, 2u, EG_H - 2u, r->start, 4u) && (tier < 4u || key_at(r, 2u, EG_H - 2u, r->start, 4u)));
}

static uint8_t stare(eg_room *r, uint8_t tier) {
  uint8_t xg, i, k, tries;
  lk = (uint8_t)(3u + eg_rndn(3u));
  xg = (uint8_t)(3u + eg_rndn(10u));
  lock_row(r, xg, 0u);
  door(r);
  for (k = 0u; k != (uint8_t)(2u + (tier >= 3u)); k++)
    eg_structure(r, (uint8_t)(lk + 2u), EG_H - 3u, eg_rndn(ST_KINDS));
  add_ob(r, (uint8_t)(OB_SENTRY | OBA_LOCK), EG_IDX(xg, lk), 0u, 0u);
  /* the start: looking up at it, in its cone, nothing on the line between */
  for (tries = 0u; tries != 40u; tries++) {
    i = open_at(r, (uint8_t)(EG_H - 4u), EG_H - 2u, 255u, 0u);
    if (!i || absd(EG_X(i), xg) > (uint8_t)(EG_Y(i) - lk - 2u) || !eg_ob_seen(r, i, 0u, EG_IDX(xg, lk))) continue;
    break;
  }
  if (tries == 40u) return 0u;
  r->start = i;
  prot(r, i);
  if (!key_at(r, 1u, (uint8_t)(lk - 1u), r->door, 3u)) return 0u;
  if (tier >= 4u && !key_at(r, (uint8_t)(lk + 2u), EG_H - 2u, r->start, 4u)) return 0u;
  if (tier >= 2u) fragile(r, 1u, T_WORN);
  return 1u;
}

uint8_t eg_gen_obstacles(eg_room *r, uint8_t tier) EG_BANKED {
  uint8_t i, x, y, ok;
  for (i = 0u; i != (uint8_t)EG_N; i++) {
    x = EG_X(i);
    y = EG_Y(i);
    r->c[i] = (x == 0u || y == 0u || x == EG_W - 1u || y == EG_H - 1u) ? T_WALL : T_EMPTY;
  }
  r->nob = r->nrun = 0u;
  r->herrings = r->depth = 0u;
  switch (r->trick) {
    case TRICK_RUSH: ok = rush(r, tier); break;
    case TRICK_STEER: ok = steer(r, tier); break;
    case TRICK_MIRROR: ok = mirror(r, tier); break;
    default: ok = stare(r, tier); break;
  }
  if (!ok) return 0u;
  /* the start and everything round it stays: protected */
  for (i = 0u; i != 4u; i++) prot(r, (uint8_t)(r->start + st4[i]));
  eg_plates_dirty = 1u;
  return (uint8_t)(r->c[r->start] == T_EMPTY);
}
