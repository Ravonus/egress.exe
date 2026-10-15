/* EGRESS room generation and gravity (see eg_room.h). Shared verbatim by the cartridge and the host tools. */
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

uint8_t eg_gravity(eg_room *r, const uint8_t *occ, uint8_t nocc) {
  uint8_t *c = r->c;
  uint8_t res = 0u, y, x, i, v, b, s;
  eg_ndirty = 0u;
  /* bottom-up, so a stack of CRTs falls together; F_MOVED keeps a CRT from moving twice in one step */
  for (y = (uint8_t)(EG_H - 2u); y != 0u; y--) {
    i = EG_IDX(1u, y);
    for (x = 1u; x != (uint8_t)(EG_W - 1u); x++, i++) {
      v = c[i];
      if ((uint8_t)(v & (T_MASK | F_MOVED)) != T_ROCK) continue;
      b = (uint8_t)(i + EG_W);
      if (c[b] == T_EMPTY) {
        if (is_occ(b, occ, nocc)) {
          if (v & F_FALL) res |= EG_G_HIT | EG_G_CHANGED;
          c[i] = T_ROCK;
          continue;
        }
        c[b] = T_ROCK | F_FALL | F_MOVED;
        c[i] = T_EMPTY;
        mark(i);
        mark(b);
        res |= EG_G_CHANGED | EG_G_FALLING;
        continue;
      }
      if (c[b] == T_ROCK) { /* resting on a resting CRT: roll left, else right */
        s = (uint8_t)(i - 1u);
        if (c[s] != T_EMPTY || c[(uint8_t)(s + EG_W)] != T_EMPTY || is_occ(s, occ, nocc) ||
            is_occ((uint8_t)(s + EG_W), occ, nocc)) {
          s = (uint8_t)(i + 1u);
          if (c[s] != T_EMPTY || c[(uint8_t)(s + EG_W)] != T_EMPTY || is_occ(s, occ, nocc) ||
              is_occ((uint8_t)(s + EG_W), occ, nocc))
            s = i;
        }
        if (s != i) {
          c[s] = T_ROCK | F_FALL | F_MOVED;
          c[i] = T_EMPTY;
          mark(i);
          mark(s);
          res |= EG_G_CHANGED | EG_G_FALLING;
          continue;
        }
      }
      if (v & F_FALL) { /* landed */
        c[i] = T_ROCK;
        res |= EG_G_CHANGED;
      }
    }
  }
  for (i = 0u; i != (uint8_t)EG_N; i++) c[i] &= (uint8_t)~F_MOVED;
  return res;
}

static uint8_t absd(uint8_t a, uint8_t b) { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }
static uint8_t dist(uint8_t a, uint8_t b) {
  return (uint8_t)(absd(EG_X(a), EG_X(b)) + absd(EG_Y(a), EG_Y(b)));
}
static uint8_t interior(void) { return EG_IDX(1u + eg_rndn(14u), 1u + eg_rndn(12u)); }
static void settle(eg_room *r) {
  uint8_t n = 96u;
  while (n-- && (eg_gravity(r, 0, 0u) & EG_G_CHANGED)) {
  }
}

static const int8_t step4[4] = {-16, 16, -1, 1};

uint8_t eg_generate(eg_room *r, uint16_t seed, uint8_t tier) {
  uint8_t *c = r->c;
  uint8_t i, k, n, x, y, t, len, s, tries, d;
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
  /* concrete wall runs */
  n = (uint8_t)(5u + tier);
  if (n > 10u) n = 10u;
  for (k = 0u; k != n; k++) {
    len = (uint8_t)(3u + eg_rndn(5u));
    s = interior();
    d = (eg_rnd() & 1u) ? 1u : 16u;
    while (len--) {
      x = EG_X(s);
      y = EG_Y(s);
      if (x == 0u || x >= EG_W - 1u || y == 0u || y >= EG_H - 1u) break;
      c[s] = T_WALL;
      s = (uint8_t)(s + d);
    }
  }
  /* open pockets: the spaces CRTs fall into and mites walk */
  n = (uint8_t)(3u + (tier >> 1));
  for (k = 0u; k != n; k++) {
    s = interior();
    len = (uint8_t)(5u + eg_rndn(10u));
    while (len--) {
      if (c[s] != T_WALL) c[s] = T_EMPTY;
      t = (uint8_t)(s + step4[eg_rnd() & 3u]);
      x = EG_X(t);
      y = EG_Y(t);
      if (x != 0u && x < EG_W - 1u && y != 0u && y < EG_H - 1u) s = t;
    }
  }
  /* CRT sets and drywall */
  for (i = EG_W; i != (uint8_t)(EG_N - EG_W); i++) {
    t = c[i];
    if (t == T_WALL) continue;
    if (eg_rndn(100u) < (uint8_t)(16u + 2u * tier))
      c[i] = T_ROCK;
    else if (t == T_DIRT && eg_rndn(100u) < 5u)
      c[i] = T_BRICK;
  }
  settle(r);
  /* the egress door in the north wall, with something walkable below it */
  x = (uint8_t)(2u + eg_rndn(12u));
  r->door = EG_IDX(x, 0u);
  c[r->door] = T_DOOR;
  s = EG_IDX(x, 1u);
  if (c[s] != T_EMPTY) c[s] = T_DIRT;
  /* the start, in the south half, with no CRT over it */
  for (tries = 0u;; tries++) {
    if (tries == 48u) return 0u;
    s = EG_IDX(1u + eg_rndn(14u), 8u + eg_rndn(5u));
    t = c[s];
    if (t == T_WALL || t == T_ROCK || c[(uint8_t)(s - EG_W)] == T_ROCK) continue;
    break;
  }
  c[s] = T_EMPTY;
  r->start = s;
  settle(r);
  if (c[s] != T_EMPTY) return 0u;
  /* keycards */
  n = tier == 0u ? 1u : (tier < 3u ? 2u : 3u);
  r->keys = 0u;
  for (tries = 0u; r->keys != n && tries != 120u; tries++) {
    i = interior();
    t = c[i];
    if ((t != T_DIRT && t != T_EMPTY) || dist(i, s) < 6u || i == (uint8_t)(r->door + EG_W)) continue;
    for (k = 0u; k != (uint8_t)EG_N; k++)
      if (c[k] == T_KEY && dist(i, k) < 4u) break;
    if (k != (uint8_t)EG_N) continue;
    c[i] = T_KEY;
    r->keys++;
  }
  if (r->keys != n) return 0u;
  /* tokens */
  n = (uint8_t)(8u + tier);
  r->tokens = 0u;
  for (tries = 0u; r->tokens != n && tries != 120u; tries++) {
    i = interior();
    t = c[i];
    if ((t != T_DIRT && t != T_EMPTY) || dist(i, s) < 2u) continue;
    c[i] = T_TOKEN;
    r->tokens++;
  }
  /* enemies: mites walk open floor, eyes float anywhere that is not concrete */
  n = (uint8_t)(2u + tier);
  if (n > EG_MAX_SPAWN) n = EG_MAX_SPAWN;
  r->nspawn = 0u;
  for (d = 0u; d != EG_MAX_SPAWN; d++) r->spawn[d] = r->spawn_kind[d] = 0u;
  for (tries = 0u; r->nspawn != n && tries != 160u; tries++) {
    i = interior();
    t = c[i];
    k = (r->nspawn & 1u) ? SPAWN_EYE : SPAWN_MITE;
    if (k == SPAWN_MITE && tries > 100u) k = SPAWN_EYE;
    if (dist(i, s) < 6u) continue;
    if (k == SPAWN_MITE ? t != T_EMPTY : (t == T_WALL || t == T_ROCK)) continue;
    for (d = 0u; d != r->nspawn; d++)
      if (dist(i, r->spawn[d]) < 3u) break;
    if (d != r->nspawn) continue;
    r->spawn[r->nspawn] = i;
    r->spawn_kind[r->nspawn] = k;
    r->nspawn++;
  }
  settle(r);
  return 1u;
}
