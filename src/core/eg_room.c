/* EGRESS room gravity, pushes and rotten carpet (see eg_room.h; the generator is eg_gen.c). Shared verbatim by the cartridge and the host tools. */
#include "eg_room.h"

uint16_t eg_rs = 1u;
uint8_t eg_dirty[EG_DIRTY_MAX];
uint8_t eg_ndirty;

uint8_t eg_rnd(void) {
  uint16_t x = eg_rs;
  x ^= x << 7;
  x ^= x >> 9;
  x ^= x << 8;
  eg_rs = x;
  return (uint8_t)(x ^ (x >> 8));
}

uint8_t eg_rndn(uint8_t n) { return (uint8_t)(((uint16_t)eg_rnd() * n) >> 8); }

static uint8_t is_occ(uint8_t i, const uint8_t *occ, uint8_t n) {
  while (n) {
    n--;
    if (occ[n] == i) return 1u;
  }
  return 0u;
}

static void mark(uint8_t i) {
  if (eg_ndirty < EG_DIRTY_MAX) eg_dirty[eg_ndirty] = i;
  if (eg_ndirty != 255u) eg_ndirty++;
}

#define ROCK_LEFT(v) ((uint8_t)(((v) & F_PLATE) ? T_PLATE : T_EMPTY))

/* drop a CRT from i into b (open floor or a pit) */
static void move_rock(uint8_t *c, uint8_t i, uint8_t b) {
  uint8_t t = c[b];
  c[i] = ROCK_LEFT(c[i]);
  if (t == T_PIT)
    c[b] = T_EMPTY | F_MOVED; /* it fills the pit */
  else
    c[b] = (uint8_t)(T_ROCK | F_FALL | F_MOVED | (t == T_PLATE ? F_PLATE : 0u));
  mark(i);
  mark(b);
}

uint8_t eg_gravity(eg_room *r, const uint8_t *occ, uint8_t nocc) {
  uint8_t *c = r->c;
  uint8_t res = 0u, y, x, i, v, b, s, w, gates;
  eg_ndirty = 0u;
  /* bottom-up, so a stack of CRTs falls together; F_MOVED keeps a CRT from moving twice in one step */
  for (y = (uint8_t)(EG_H - 2u); y != 0u; y--) {
    i = EG_IDX(1u, y);
    for (x = 1u; x != (uint8_t)(EG_W - 1u); x++, i++) {
      v = c[i];
      if ((uint8_t)(v & (T_MASK | F_MOVED)) != T_ROCK) continue;
      b = (uint8_t)(i + EG_W);
      if (EG_ROCK_OPEN(c[b]) || c[b] == T_PIT) {
        if (is_occ(b, occ, nocc)) {
          if (v & F_FALL) res |= EG_G_HIT | EG_G_CHANGED;
          c[i] = (uint8_t)(v & (uint8_t)~F_FALL);
          continue;
        }
        move_rock(c, i, b);
        res |= EG_G_CHANGED | EG_G_FALLING;
        continue;
      }
      if ((uint8_t)(c[b] & (T_MASK | F_FALL)) == T_ROCK) { /* resting on a resting CRT: roll left, else right */
        s = (uint8_t)(i - 1u);
        if (!EG_ROCK_OPEN(c[s]) || !EG_ROCK_OPEN(c[(uint8_t)(s + EG_W)]) || is_occ(s, occ, nocc) ||
            is_occ((uint8_t)(s + EG_W), occ, nocc)) {
          s = (uint8_t)(i + 1u);
          if (!EG_ROCK_OPEN(c[s]) || !EG_ROCK_OPEN(c[(uint8_t)(s + EG_W)]) || is_occ(s, occ, nocc) ||
              is_occ((uint8_t)(s + EG_W), occ, nocc))
            s = i;
        }
        if (s != i) {
          move_rock(c, i, s);
          res |= EG_G_CHANGED | EG_G_FALLING;
          continue;
        }
      }
      if ((v & F_FALL) && (c[b] == T_GLASS || c[b] == T_CEIL || c[b] == T_CEIL_1)) { /* through it, into the void */
        c[b] = T_PIT;
        c[i] = ROCK_LEFT(v);
        mark(i);
        mark(b);
        res |= EG_G_CHANGED | EG_G_BROKE;
        continue;
      }
      if (v & F_FALL) { /* landed */
        c[i] = (uint8_t)(v & (uint8_t)~F_FALL);
        res |= EG_G_CHANGED;
      }
    }
  }
  /* shutters follow the plates: up while any plate carries a CRT (a shutter doesn't drop on the player) */
  w = 0u;
  gates = 0u;
  for (i = 0u; i != (uint8_t)EG_N; i++) {
    v = (uint8_t)(c[i] & (uint8_t)~F_MOVED);
    c[i] = v;
    if ((uint8_t)(v & (T_MASK | F_PLATE)) == (T_ROCK | F_PLATE) || v == T_BLOCK_PLATE) w = 1u;
    if (v == T_GATE || v == T_GATE_OPEN) gates = 1u;
  }
  if (gates)
    for (i = 0u; i != (uint8_t)EG_N; i++) {
      v = c[i];
      if (w && v == T_GATE) {
        c[i] = T_GATE_OPEN;
        mark(i);
        res |= EG_G_CHANGED;
      } else if (!w && v == T_GATE_OPEN && !is_occ(i, occ, nocc)) {
        c[i] = T_GATE;
        mark(i);
        res |= EG_G_CHANGED;
      }
    }
  return res;
}

uint8_t eg_push(eg_room *r, uint8_t rock, uint8_t to) {
  uint8_t t = r->c[to];
  if (t == T_GLASS || t == T_CEIL || t == T_CEIL_1) { /* it smashes through into the void: both gone */
    r->c[to] = T_PIT;
    r->c[rock] = ROCK_LEFT(r->c[rock]);
    return 1u;
  }
  if (!EG_ROCK_OPEN(t) && t != T_PIT) return 0u;
  r->c[to] = t == T_PIT ? T_EMPTY : (uint8_t)(T_ROCK | (t == T_PLATE ? F_PLATE : 0u));
  r->c[rock] = ROCK_LEFT(r->c[rock]);
  return 1u;
}

uint8_t eg_leave(eg_room *r, uint8_t cell) { return eg_step_off(r, cell, BB_PLAYER, 1u); }

/* --- fragile floor --- */
#define ALL_MOBS (BB_MOB_WALKER | BB_MOB_HEAVY | BB_MOB_BURROWER)
const eg_frag_kind eg_frag[FK_COUNT] = {
    /* base       steps reset    ticks breakby                                                              flags */
    {T_CRUMBLE, 1u, FR_ROOM, 0u, BB_PLAYER | BB_HEAVY | BB_FALLING | BB_BOSS | ALL_MOBS, 0u},
    {T_WORN, 3u, FR_ROOM, 0u, BB_PLAYER | BB_HEAVY | BB_MOB_HEAVY | BB_MOB_BURROWER | BB_BOSS | BB_REACTION, 0u},
    {T_CEIL, 2u, FR_ROOM, 0u, BB_PLAYER | BB_HEAVY | BB_FALLING | BB_BOSS | ALL_MOBS | BB_REACTION, 0u},
    {T_GLASS, 1u, FR_ROOM, 0u, BB_PLAYER | BB_HEAVY | BB_FALLING | BB_MOB_HEAVY | BB_BOSS, FF_SEE},
    {T_GLITCH, 0u, FR_TIMED, 30u, BB_FALLING | BB_MOB_HEAVY | BB_BOSS, FF_SEE | FF_PHASED},
    {T_ICE, 2u, FR_ROOM, 0u, BB_PLAYER | BB_HEAVY | BB_FALLING | BB_MOB_HEAVY | BB_BOSS | BB_REACTION, 0u},
};

uint8_t eg_frag_kind_of(uint8_t t) {
  if (t == T_CRUMBLE) return FK_CRUMBLE;
  if (t == T_GLITCH) return FK_GLITCH;
  if (t >= T_WORN && t <= T_WORN_2) return FK_WORN;
  if (t == T_CEIL || t == T_CEIL_1) return FK_CEIL;
  if (t == T_GLASS) return FK_GLASS;
  if (t == T_ICE || t == T_ICE_1) return FK_ICE;
  return FK_NONE;
}

uint8_t eg_step_off(eg_room *r, uint8_t cell, uint8_t who, uint8_t weight) {
  uint8_t t = r->c[cell], k = eg_frag_kind_of(t), stage;
  if (k == FK_NONE || !eg_frag[k].steps || !(eg_frag[k].breakby & who)) return 0u;
  stage = (uint8_t)(t - eg_frag[k].base + weight);
  if (stage >= eg_frag[k].steps) {
    r->c[cell] = T_PIT;
    return 1u;
  }
  r->c[cell] = (uint8_t)(eg_frag[k].base + stage);
  return 0u;
}

uint8_t eg_frag_hit(eg_room *r, uint8_t cell, uint8_t who) {
  uint8_t k = eg_frag_kind_of(r->c[cell]);
  if (k == FK_NONE || EG_PROT(r, cell) || !(eg_frag[k].breakby & who)) return 0u;
  r->c[cell] = T_PIT;
  return 1u;
}

uint8_t eg_dead(const eg_room *r, uint8_t from, const uint8_t *pocket, uint8_t npocket) {
  static uint8_t seen[EG_N / 8u], q[EG_N];
  uint8_t pass, n, h, i, s, t, d, rock = 0u, k, j, p;
  static const int8_t st[4] = {-16, 16, -1, 1};
  /* pass 0 floods without crossing pits; if it reached a CRT, pass 1 floods again letting pits through */
  for (pass = 0u; pass != 2u; pass++) {
    for (i = 0u; i != (uint8_t)(EG_N / 8u); i++) seen[i] = 0u;
    n = h = 0u;
    q[n++] = from;
    seen[from >> 3] |= (uint8_t)(1u << (from & 7u));
    while (h != n) {
      i = q[h++];
      for (d = 0u; d != 4u; d++) {
        s = (uint8_t)(i + (uint8_t)st[d]);
        if ((d == 0u && i < EG_W) || (d == 1u && i >= (uint8_t)(EG_N - EG_W)) || (d == 2u && !(i & 15u)) ||
            (d == 3u && (i & 15u) == 15u))
          continue;
        if (seen[s >> 3] & (uint8_t)(1u << (s & 7u))) continue;
        t = (uint8_t)(r->c[s] & T_MASK);
        if (t == T_WALL || t == T_SOCKET || t == T_SOCKET_ON || (t == T_PIT && !rock)) continue;
        seen[s >> 3] |= (uint8_t)(1u << (s & 7u));
        if (t == T_ROCK) rock = 1u;
        if (t != T_DOOR) q[n++] = s; /* the door is a goal, not a way through */
      }
    }
    if (!rock || pass) break;
  }
#define SEEN(c) (seen[(uint8_t)(c) >> 3] & (uint8_t)(1u << ((c) & 7u)))
  if (!SEEN(r->door)) return EG_DEAD_DOOR;
  i = 0u;
  do {
    if ((r->c[i] & T_MASK) == T_KEY && !SEEN(i)) return EG_DEAD_KEY;
  } while (++i != (uint8_t)EG_N);
  /* a piece the room needs (not a herring) out of reach, with no twin in reach and none in the pocket */
  for (k = 0u; k != (uint8_t)(r->nitem - r->herrings); k++) {
    i = r->item_cell[k];
    if ((r->c[i] & T_MASK) != T_ITEM || SEEN(i)) continue;
    p = r->item_piece[k];
    for (j = 0u; j != npocket; j++)
      if (pocket[j] == p) break;
    if (j != npocket) continue;
    for (j = 0u; j != r->nitem; j++)
      if (r->item_piece[j] == p && (r->c[r->item_cell[j]] & T_MASK) == T_ITEM && SEEN(r->item_cell[j])) break;
    if (j == r->nitem) return EG_DEAD_ITEM;
  }
#undef SEEN
  return 0u;
}

uint8_t eg_count(const eg_room *r, uint8_t type) {
  uint8_t i = 0u, n = 0u;
  do {
    if ((r->c[i] & T_MASK) == type) n++;
  } while (++i != (uint8_t)EG_N);
  return n;
}

uint8_t eg_item_at(const eg_room *r, uint8_t cell) {
  uint8_t k;
  for (k = 0u; k != r->nitem; k++)
    if (r->item_cell[k] == cell) return r->item_piece[k];
  return 0u;
}
