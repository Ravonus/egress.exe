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
    if ((uint8_t)(v & (T_MASK | F_PLATE)) == (T_ROCK | F_PLATE)) w = 1u;
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
  /* tier 2 up: a shutter under the door, opened by a CRT on a plate somewhere below */
  if (tier >= 2u && (tier >= 4u || (eg_rnd() & 1u))) {
    x = EG_X(r->door);
    c[EG_IDX(x, 1u)] = T_GATE;
    c[EG_IDX(x - 1u, 1u)] = T_WALL;
    c[EG_IDX(x + 1u, 1u)] = T_WALL;
    if (c[EG_IDX(x, 2u)] == T_WALL || c[EG_IDX(x, 2u)] == T_ROCK) c[EG_IDX(x, 2u)] = T_DIRT;
    for (tries = 0u;; tries++) {
      if (tries == 80u) return 0u;
      i = EG_IDX(1u + eg_rndn(14u), 4u + eg_rndn(9u));
      t = c[i];
      if ((t != T_EMPTY && t != T_DIRT) || i == s || dist(i, r->door) < 5u) continue;
      t = c[(uint8_t)(i + EG_W)];
      if (t == T_EMPTY || t == T_PIT) continue; /* a CRT must be able to rest on it */
      c[i] = T_PLATE;
      break;
    }
  }
  /* tier 1 up: rotten carpet; tier 3 up: pits to fill */
  n = tier ? (uint8_t)(2u + tier) : 0u;
  for (tries = 0u; n && tries != 100u; tries++) {
    i = interior();
    if (c[i] != T_EMPTY || i == s || dist(i, s) < 2u || i == (uint8_t)(r->door + EG_W)) continue;
    c[i] = T_CRUMBLE;
    n--;
  }
  n = tier >= 3u ? (uint8_t)(tier - 2u) : 0u;
  for (tries = 0u; n && tries != 100u; tries++) {
    i = interior();
    if (c[i] != T_EMPTY || dist(i, s) < 3u || EG_Y(i) < 2u) continue;
    c[i] = T_PIT;
    n--;
  }
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
