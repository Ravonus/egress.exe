/* EGRESS living obstacles (see eg_obst.h). Shared verbatim by the cartridge and the host tick solver. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_obst.h"

const uint8_t eg_ob_cost[OB_RULES] = {2u, 3u, 3u, 2u, 4u, 3u, 2u, 4u, 2u, 2u};

static const int8_t ostep[4] = {-16, 1, 16, -1}; /* N E S W */
static const int8_t fx8[8] = {0, 1, 1, 1, 0, -1, -1, -1}, fy8[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

static eg_otick *tk; /* the tick being run */

static void chg(uint8_t c) {
  uint8_t k;
  tk->ev |= EV_TILES;
  for (k = 0u; k != tk->nchg; k++)
    if (tk->chg[k] == c) return;
  if (tk->nchg < 6u) tk->chg[tk->nchg++] = c;
}

uint8_t eg_ob_walk(uint8_t v, uint8_t rider) {
  if (v & F_OB) return 0u;
  if (v == T_EMPTY || v == T_PLATE || v == T_GATE_OPEN || v == T_PLANK || v == T_KEY || v == T_TOKEN ||
      v == T_ITEM || v == T_GLITCH || EG_FRAGILE(v))
    return 1u;
  return (uint8_t)(rider && v == T_PIT);
}

/* plain floor the player can be shoved onto (anything else pins them) */
static uint8_t shove_ok(uint8_t v) {
  return (uint8_t)(v == T_EMPTY || v == T_PLATE || v == T_GATE_OPEN || v == T_PLANK || EG_FRAGILE(v));
}

static uint8_t absd(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }

uint8_t eg_route_len(const eg_room *r) {
  uint8_t k, n = 0u;
  for (k = 0u; k != r->nrun; k++) n = (uint8_t)(n + (r->run[k] & 63u));
  return n;
}

/* the direction of route step `pos` */
static uint8_t route_dir(const eg_room *r, uint8_t pos) {
  uint8_t k, n;
  for (k = 0u; k != r->nrun; k++) {
    n = (uint8_t)(r->run[k] & 63u);
    if (pos < n) return (uint8_t)(r->run[k] >> 6);
    pos = (uint8_t)(pos - n);
  }
  return 0u;
}

uint8_t eg_ob_at(const eg_room *r, const eg_ob *ob, uint8_t cell) {
  uint8_t k;
  for (k = 0u; k != r->nob; k++)
    if (ob[k].cell == cell) return k;
  return 255u;
}

static uint8_t rule_of(const eg_room *r, const eg_ob *o, uint8_t k) {
  uint8_t ru = OBA_RULE(r->ob[k][0]);
  if (ru == OB_SLEEPER && (o->st & OBF_AWAKE) && !(o->st & OBF_YAWN))
    return OBB_VAR(r->ob[k][2]) ? OB_HEAVY : OB_PATROL;
  return ru;
}

void eg_ob_init(eg_room *r, eg_ob *ob) EG_BANKED {
  uint8_t k, b;
  eg_ob *o;
  for (k = 0u; k != r->nob; k++) {
    o = &ob[k];
    b = r->ob[k][2];
    o->cell = o->from = r->ob[k][1];
    o->dir = OBB_DIR(b);
    o->pos = OBP_ARG(r->ob[k][3]);
    o->stop = o->imm = o->belly = 0u;
    o->st = (uint8_t)((b & OBB_ASLEEP) ? 0u : OBF_AWAKE);
    if (OBA_RULE(r->ob[k][0]) == OB_FOLLOWER) o->st = 0u; /* dormant */
    r->c[o->cell] |= F_OB;
  }
}

uint8_t eg_ob_stop(eg_ob *o, uint8_t kind, uint8_t ticks) EG_BANKED {
  if (o->stop || o->imm) return 0u;
  o->stop = (uint8_t)((kind << 4) | (ticks & 15u));
  return 1u;
}

void eg_ob_reverse(const eg_room *r, eg_ob *o, uint8_t k) EG_BANKED {
  uint8_t v;
  o->dir = (uint8_t)(((o->dir + 2u) & 3u) | ((o->dir ^ OBD_REV) & OBD_REV));
  if (OBA_RULE(r->ob[k][0]) == OB_MIRROR) {
    v = OBB_VAR(r->ob[k][2]);
    if (v == 1u || v == 2u) o->belly ^= 0x80u; /* the axis swapped (kept in the belly's top bit: a mirror eats nothing) */
  }
}

/* sight blockers: what a Bresenham line may not cross */
static uint8_t opaque(uint8_t v) {
  uint8_t t = (uint8_t)(v & T_MASK);
  return (uint8_t)(t == T_WALL || t == T_PARTITION || t == T_GATE || t == T_DOOR || t == T_BLOCK ||
                   t == T_BLOCK_PLATE || t == T_PAPER || t == T_BRICK || t == T_SOCKET || t == T_SOCKET_ON ||
                   t == T_DEAD || t == T_ROCK);
}

uint8_t eg_ob_seen(const eg_room *r, uint8_t p, uint8_t face, uint8_t s) EG_BANKED {
  int8_t vx = (int8_t)(EG_X(s) - EG_X(p)), vy = (int8_t)(EG_Y(s) - EG_Y(p)), fx = fx8[face & 7u], fy = fy8[face & 7u];
  int8_t dot = (int8_t)(vx * fx + vy * fy), cr = (int8_t)(vx * fy - vy * fx), x, y, sx, sy, ax, ay, e, e2;
  if (dot <= 0) return 0u;
  if (cr < 0) cr = (int8_t)-cr;
  if (cr > dot) return 0u;
  /* the line between the cell centres (Bresenham), endpoints excluded */
  x = (int8_t)EG_X(p);
  y = (int8_t)EG_Y(p);
  ax = vx < 0 ? (int8_t)-vx : vx;
  ay = vy < 0 ? (int8_t)-vy : vy;
  sx = vx < 0 ? -1 : 1;
  sy = vy < 0 ? -1 : 1;
  e = (int8_t)(ax - ay);
  for (;;) {
    e2 = (int8_t)(e + e);
    if (e2 > -ay) {
      e = (int8_t)(e - ay);
      x = (int8_t)(x + sx);
    }
    if (e2 < ax) {
      e = (int8_t)(e + ax);
      y = (int8_t)(y + sy);
    }
    if (EG_IDX(x, y) == s) return 1u;
    if (opaque(r->c[EG_IDX(x, y)])) return 0u;
  }
}

/* leave `from` (its F_OB off, the step-off crack unless it rides or is protected) */
static void leave(eg_room *r, uint8_t from, uint8_t ru, uint8_t crack) {
  uint8_t v;
  r->c[from] &= (uint8_t)~F_OB;
  v = r->c[from];
  if (v == T_PLATE || v == T_GATE_OPEN) eg_plates_dirty = 1u;
  if (!crack || ru == OB_PATROL || EG_PROT(r, from)) return;
  if (ru == OB_HEAVY)
    eg_step_off(r, from, BB_MOB_HEAVY, 2u);
  else
    eg_step_off(r, from, BB_MOB_WALKER, 1u);
  if (r->c[from] != v) chg(from);
}

static void enter(eg_room *r, eg_ob *o, uint8_t to) {
  uint8_t v = r->c[to];
  if (v == T_PARTITION) {
    r->c[to] = v = T_EMPTY;
    tk->ev |= EV_BROKE;
    chg(to);
  }
  if (v == T_PLATE || v == T_GATE_OPEN) eg_plates_dirty = 1u;
  r->c[to] = (uint8_t)(v | F_OB);
  o->from = o->cell;
  o->cell = to;
  o->st |= OBF_MOVED;
  tk->ev |= EV_MOVED;
}

/* the sentry's catch: back to its plinth (if the plinth is free; else it holds where it is) */
static void catch_player(eg_room *r, eg_ob *o, uint8_t k) {
  uint8_t pl = r->ob[k][1];
  tk->hits++;
  tk->ev |= EV_HIT | EV_CATCH;
  if (pl == tk->p || (r->c[pl] & F_OB) || !eg_ob_walk(r->c[pl], 0u)) return;
  leave(r, o->cell, OB_SENTRY, 0u);
  enter(r, o, pl);
  o->from = pl; /* a static burst, not a walk */
}

/* one step of slot k toward direction d. Returns 1 when the step happened (moved, shoved, trampled, caught). */
static uint8_t try_step(eg_room *r, eg_ob *o, uint8_t k, uint8_t ru, uint8_t d) {
  uint8_t to = (uint8_t)(o->cell + ostep[d]), v = r->c[to], s, from = o->cell;
  if (to == tk->p) {
    if (ru == OB_SENTRY) {
      catch_player(r, o, k);
      return 1u;
    }
    s = (uint8_t)(to + ostep[d]);
    if (shove_ok(r->c[s])) { /* shoved one cell along its path; the cell they left takes its crack */
      tk->p = s;
      tk->ev |= EV_SHOVE;
      if (eg_step_off(r, to, BB_PLAYER, 1u)) chg(to);
      leave(r, from, ru, 1u);
      enter(r, o, to);
      return 1u;
    }
    /* pinned: trampled (1 hit) and the two swap; its own crack waits for the player to leave its old cell. (A rider
     * over a pit can't swap: it is blocked instead.) */
    v = (uint8_t)(r->c[from] & (uint8_t)~F_OB);
    if (!EG_PASSABLE(v)) return 0u;
    tk->hits++;
    tk->ev |= EV_HIT | EV_SHOVE;
    tk->p = from;
    if (eg_step_off(r, to, BB_PLAYER, 1u)) chg(to);
    leave(r, from, ru, 0u);
    enter(r, o, to);
    return 1u;
  }
  if (eg_ob_walk(v, (uint8_t)(ru == OB_PATROL))) {
    leave(r, from, ru, 1u);
    enter(r, o, to);
    return 1u;
  }
  if (v == T_PARTITION && ru == OB_HEAVY) {
    leave(r, from, ru, 1u);
    enter(r, o, to);
    return 1u;
  }
  if (EG_CRATE(v) && (ru == OB_PUSHER || ru == OB_MIRROR)) {
    s = (uint8_t)(to + ostep[d]);
    if (s != tk->p && eg_push_block(r, to, s)) {
      chg(to);
      chg(s);
      tk->ev |= EV_PUSH;
      leave(r, from, ru, 1u);
      enter(r, o, to);
      return 1u;
    }
  }
  return 0u;
}

/* a clear line along a row or column from a to b (exclusive): nothing solid between */
static uint8_t clear_line(const eg_room *r, uint8_t a, uint8_t b, uint8_t d) {
  uint8_t c = (uint8_t)(a + ostep[d]);
  while (c != b) {
    if (opaque(r->c[c]) || (r->c[c] & T_MASK) == T_PIT) return 0u;
    c = (uint8_t)(c + ostep[d]);
  }
  return 1u;
}

/* the direction toward the nearest lure in the row or column within `reach` (ties N, E, S, W), else 255 */
static uint8_t lure_dir(const eg_room *r, uint8_t at, uint8_t reach, uint8_t food) {
  uint8_t best = 255u, bd = 255u, d, n, c, i, x = EG_X(at), y = EG_Y(at);
  for (i = 0u; i != tk->nnoise; i++) {
    c = tk->noise[i];
    if (c == at) continue;
    if (EG_X(c) == x) {
      n = absd(EG_Y(c), y);
      d = EG_Y(c) < y ? 0u : 2u;
    } else if (EG_Y(c) == y) {
      n = absd(EG_X(c), x);
      d = EG_X(c) > x ? 1u : 3u;
    } else
      continue;
    if (n > reach || !clear_line(r, at, c, d)) continue;
    if (n < bd || (n == bd && d < best)) bd = n, best = d;
  }
  if (food) /* the eater: tokens and pieces lying within EG_OB_FOOD */
    for (d = 0u; d != 4u; d++) {
      c = at;
      for (n = 1u; n <= EG_OB_FOOD && n < bd; n++) {
        c = (uint8_t)(c + ostep[d]);
        i = r->c[c];
        if (i == T_TOKEN || i == T_ITEM) {
          if (n < bd || (n == bd && d < best)) bd = n, best = d;
          break;
        }
        if (opaque(i) || (i & T_MASK) == T_PIT) break;
      }
    }
  return best;
}

static uint8_t turn(const eg_room *r, uint8_t k, uint8_t d) {
  return (uint8_t)((d + ((r->ob[k][0] & OBA_LEFT) ? 3u : 1u)) & 3u);
}

void eg_ob_tick(eg_room *r, eg_ob *ob, eg_otick *t) EG_BANKED {
  uint8_t k, ru, a, d, n, sense[EG_MAX_OB], p = t->p;
  int8_t dx, dy;
  eg_ob *o;
  tk = t;
  t->hits = t->ev = t->nchg = 0u;
  /* 2 statuses */
  for (k = 0u; k != r->nob; k++) {
    o = &ob[k];
    o->st &= (uint8_t)~OBF_MOVED;
    o->from = o->cell;
    if (o->stop) {
      o->stop--;
      if (!(o->stop & 15u)) {
        o->stop = 0u;
        o->imm = EG_OB_IMMUNE;
      }
    } else if (o->imm)
      o->imm--;
    if (o->st & OBF_YAWN) o->st = (uint8_t)(o->st - 2u);
  }
  /* 3 sense: everyone against the latched state */
  for (k = 0u; k != r->nob; k++) {
    o = &ob[k];
    ru = rule_of(r, o, k);
    sense[k] = 255u;
    if (o->stop) continue;
    if (OBA_RULE(r->ob[k][0]) == OB_SLEEPER && !(o->st & OBF_AWAKE)) {
      n = OBP_ARG(r->ob[k][3]);
      if (!n || OBB_VAR(r->ob[k][2]) == 0u) n = EG_OB_WAKE; /* variant 0's argument is its route position */
      for (a = 0u; a != t->nnoise; a++) {
        d = t->noise[a];
        if (absd(EG_X(d), EG_X(o->cell)) <= n && absd(EG_Y(d), EG_Y(o->cell)) <= n) {
          o->st = OBF_AWAKE | 0x04u; /* a yawn: it holds this tick and the next, then acts */
          t->ev |= EV_WOKE;
          break;
        }
      }
      continue;
    }
    if (ru == OB_FOLLOWER && !(o->st & OBF_AWAKE)) {
      if (absd(EG_X(p), EG_X(o->cell)) <= 1u && absd(EG_Y(p), EG_Y(o->cell)) <= 1u) o->st |= OBF_AWAKE;
      continue;
    }
    if (ru == OB_SENTRY) sense[k] = eg_ob_seen(r, p, t->face, o->cell);
    if (ru == OB_HEAVY || ru == OB_EATER) sense[k] = lure_dir(r, o->cell, EG_OB_LURE, (uint8_t)(ru == OB_EATER));
  }
  /* 4 act, slot order */
  for (k = 0u; k != r->nob; k++) {
    o = &ob[k];
    a = r->ob[k][0];
    if (o->stop) continue;
    if ((a & OBA_P2) && (uint8_t)(t->tick & 1u) != (uint8_t)((r->ob[k][3] & OBP_PHASE) ? 1u : 0u)) continue;
    ru = rule_of(r, o, k);
    if (ru == OB_SLEEPER) continue; /* asleep or yawning: holds */
    switch (ru) {
      case OB_PATROL:
        n = eg_route_len(r);
        if (!n) break;
        if (o->dir & OBD_REV) {
          a = (uint8_t)(o->pos ? o->pos - 1u : n - 1u);
          d = (uint8_t)((route_dir(r, a) + 2u) & 3u);
        } else {
          a = (uint8_t)(o->pos + 1u == n ? 0u : o->pos + 1u);
          d = route_dir(r, o->pos);
        }
        if (try_step(r, o, k, ru, d))
          o->pos = a;
        else
          o->dir ^= OBD_REV; /* reverses; holds this tick */
        break;
      case OB_HEAVY:
      case OB_PUSHER:
      case OB_EATER:
        if (sense[k] != 255u) o->dir = (uint8_t)((o->dir & OBD_REV) | sense[k]);
        d = (uint8_t)(o->dir & 3u);
        if ((o->stop >> 4) == OBS_ROOT || !try_step(r, o, k, ru, d)) {
          o->dir = (uint8_t)((o->dir & OBD_REV) | turn(r, k, d));
          break;
        }
        if (ru == OB_EATER && o->cell != o->from) {
          n = r->c[o->cell] & T_MASK;
          if ((n == T_TOKEN || n == T_ITEM) && o->belly < 4u) {
            r->c[o->cell] = T_EMPTY | F_OB;
            o->belly++;
            chg(o->cell);
          }
        }
        break;
      case OB_MIRROR:
        if (p == t->pp) break;
        dx = (int8_t)(EG_X(p) - EG_X(t->pp));
        dy = (int8_t)(EG_Y(p) - EG_Y(t->pp));
        dx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
        dy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
        n = OBB_VAR(r->ob[k][2]);
        if (o->belly & 0x80u) n = (uint8_t)(n ^ 3u); /* reversed: the other axis */
        if (n & 1u) dx = (int8_t)-dx;
        if (n & 2u) dy = (int8_t)-dy;
        if (dx) try_step(r, o, k, ru, dx > 0 ? 1u : 3u); /* x, then y: a blocked axis holds */
        if (dy) try_step(r, o, k, ru, dy > 0 ? 2u : 0u);
        break;
      case OB_SENTRY:
        if (sense[k]) break; /* looked at: still */
        dx = (int8_t)(EG_X(t->p) - EG_X(o->cell));
        dy = (int8_t)(EG_Y(t->p) - EG_Y(o->cell));
        a = (uint8_t)(dx < 0 ? -dx : dx);
        n = (uint8_t)(dy < 0 ? -dy : dy);
        d = dx > 0 ? 1u : 3u;  /* the x step */
        ru = dy > 0 ? 2u : 0u; /* the y step */
        if (a >= n) {
          if (!(a && try_step(r, o, k, OB_SENTRY, d)) && !(n && try_step(r, o, k, OB_SENTRY, ru))) break;
        } else if (!try_step(r, o, k, OB_SENTRY, ru) && !(a && try_step(r, o, k, OB_SENTRY, d)))
          break;
        o->belly = (uint8_t)((o->belly + 1u) % 3u); /* a new pose with every unseen move */
        break;
      case OB_FOLLOWER:
        if (!(o->st & OBF_AWAKE) || t->pp == p) break;
        for (d = 0u; d != 4u; d++)
          if ((uint8_t)(o->cell + ostep[d]) == t->pp) {
            try_step(r, o, k, ru, d);
            break;
          }
        break;
      default: /* the light-eater and the drifter are not built (B2): they hold */
        break;
    }
  }
}
