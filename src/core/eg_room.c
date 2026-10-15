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
  if (!EG_ROCK_OPEN(t) && t != T_PIT) return 0u;
  r->c[to] = t == T_PIT ? T_EMPTY : (uint8_t)(T_ROCK | (t == T_PLATE ? F_PLATE : 0u));
  r->c[rock] = ROCK_LEFT(r->c[rock]);
  return 1u;
}

uint8_t eg_leave(eg_room *r, uint8_t cell) {
  if (r->c[cell] != T_CRUMBLE) return 0u;
  r->c[cell] = T_PIT;
  return 1u;
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
