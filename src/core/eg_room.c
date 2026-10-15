/* EGRESS room gravity, pushes and rotten carpet (see eg_room.h; the generator is eg_gen.c). Shared verbatim by the cartridge and the host tools. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_room.h"

uint16_t eg_rs = 1u;
uint8_t eg_dirty[EG_DIRTY_MAX];
uint8_t eg_ndirty;

/* near versions for this file (on the cartridge the public ones are banked calls) */
static uint8_t rnd_next(void) {
  uint16_t x = eg_rs;
  x ^= x << 7;
  x ^= x >> 9;
  x ^= x << 8;
  eg_rs = x;
  return (uint8_t)(x ^ (x >> 8));
}

uint8_t eg_rnd(void) EG_BANKED { return rnd_next(); }
uint8_t eg_rndn(uint8_t n) EG_BANKED { return (uint8_t)(((uint16_t)rnd_next() * n) >> 8); }

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

#if defined(__SDCC) && defined(__PORT_sm83)
/* The cartridge's two hot loops in SM83 (each ~25-30 scan lines as SDCC C, every GRAVITY_FRAMES): the scan for the
 * next CRT that may move, and the F_MOVED clear. The host builds the same C as before. */
static const uint8_t *scan_p;
static uint8_t scan_n;
static uint8_t scan_rock(void) __naked { /* cells skipped from scan_p before one with (v & 0x5f) == T_ROCK; scan_n if none */
  __asm
    ld hl, #_scan_p
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld a, (#_scan_n)
    ld c, a
    ld b, #0
  1$:
    ld a, b
    cp a, c
    ret z
    ld a, (hl+)
    and a, #0x5f
    cp a, #0x04
    jr z, 2$
    inc b
    jr 1$
  2$:
    ld a, b
    ret
  __endasm;
}
static void clear_moved(void) __naked { /* EG_N cells from scan_p: F_MOVED off */
  __asm
    ld hl, #_scan_p
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld c, #224
  1$:
    ld a, (hl)
    and a, #0xbf
    ld (hl+), a
    dec c
    jr nz, 1$
    ret
  __endasm;
}
#if EG_N != 224u || F_MOVED != 0x40u || T_MASK != 0x1fu /* and T_ROCK == 4 (an enum: checked by eye) */
#error "eg_gravity: the SM83 loops hard-code EG_N, F_MOVED, T_MASK and T_ROCK"
#endif
#define EG_GRAV_ASM 1
#endif

#define ROCK_LEFT(v) ((uint8_t)(((v) & F_PLATE) ? T_PLATE : T_EMPTY))

uint8_t eg_plates_dirty = 1u;

/* drop a CRT from i into b (open floor or a pit) */
static void move_rock(uint8_t *c, uint8_t i, uint8_t b) {
  uint8_t t = c[b];
  if ((c[i] & F_PLATE) || t == T_PLATE) eg_plates_dirty = 1u; /* off a plate, or onto one */
  c[i] = ROCK_LEFT(c[i]);
  if (t == T_PIT)
    c[b] = T_EMPTY | F_MOVED; /* it fills the pit */
  else
    c[b] = (uint8_t)(T_ROCK | F_FALL | F_MOVED | (t == T_PLATE ? F_PLATE : 0u));
  mark(i);
  mark(b);
}

uint8_t eg_gravity(eg_room *r, const uint8_t *occ, uint8_t nocc) EG_BANKED {
  uint8_t *c = r->c;
  uint8_t res = 0u, y, x, i, v, b, s, w, gates;
  eg_ndirty = 0u;
  /* bottom-up, so a stack of CRTs falls together; F_MOVED keeps a CRT from moving twice in one step */
  for (y = (uint8_t)(EG_H - 2u); y != 0u; y--) {
    i = EG_IDX(1u, y);
    x = (uint8_t)(EG_W - 2u);
    while (x) {
#ifdef EG_GRAV_ASM
      scan_p = &c[i];
      scan_n = x;
      v = scan_rock();
      i = (uint8_t)(i + v);
      x = (uint8_t)(x - v);
      if (!x) break;
#endif
      v = c[i];
      if ((uint8_t)(v & (T_MASK | F_MOVED)) != T_ROCK) goto next;
      b = (uint8_t)(i + EG_W);
      if (EG_ROCK_OPEN(c[b]) || c[b] == T_PIT) {
        if (is_occ(b, occ, nocc)) {
          if (v & F_FALL) res |= EG_G_HIT | EG_G_CHANGED;
          c[i] = (uint8_t)(v & (uint8_t)~F_FALL);
          goto next;
        }
        move_rock(c, i, b);
        res |= EG_G_CHANGED | EG_G_FALLING;
        goto next;
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
          goto next;
        }
      }
      if ((v & F_FALL) && (c[b] == T_GLASS || c[b] == T_CEIL || c[b] == T_CEIL_1)) { /* through it, into the void */
        c[b] = T_PIT;
        if (v & F_PLATE) eg_plates_dirty = 1u;
        c[i] = ROCK_LEFT(v);
        mark(i);
        mark(b);
        res |= EG_G_CHANGED | EG_G_BROKE;
        goto next;
      }
      if (v & F_FALL) { /* landed */
        c[i] = (uint8_t)(v & (uint8_t)~F_FALL);
        res |= EG_G_CHANGED;
      }
    next:
      i++;
      x--;
    }
  }
#ifdef EG_GRAV_ASM
  scan_p = c;
  clear_moved();
#else
  for (i = 0u; i != (uint8_t)EG_N; i++) c[i] &= (uint8_t)~F_MOVED;
#endif
  /* shutters follow the plates: up while any plate carries a CRT (a shutter doesn't drop on the player) */
#ifdef EG_GRAV_ASM
  if (!eg_plates_dirty) return res; /* no CRT came onto or off a plate: the shutters stay as they are */
  eg_plates_dirty = 0u;
#endif
  w = 0u;
  gates = 0u;
  for (i = 0u; i != (uint8_t)EG_N; i++) {
    v = c[i];
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
      } else if (!w && v == T_GATE_OPEN) {
        if (is_occ(i, occ, nocc)) {
          eg_plates_dirty = 1u; /* try again once the cell is clear */
          continue;
        }
        c[i] = T_GATE;
        mark(i);
        res |= EG_G_CHANGED;
      }
    }
  return res;
}

uint8_t eg_push(eg_room *r, uint8_t rock, uint8_t to) EG_BANKED {
  uint8_t t = r->c[to];
  if ((r->c[rock] & F_PLATE) || t == T_PLATE) eg_plates_dirty = 1u; /* off a plate, or onto one */
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

uint8_t eg_leave(eg_room *r, uint8_t cell) EG_BANKED { return eg_step_off(r, cell, BB_PLAYER, 1u); }

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

uint8_t eg_frag_ticks(uint8_t k) EG_BANKED {
  return (k < FK_COUNT && eg_frag[k].reset == FR_TIMED) ? eg_frag[k].reset_ticks : 0u;
}

static uint8_t frag_kind_of(uint8_t t) {
  if (t == T_CRUMBLE) return FK_CRUMBLE;
  if (t == T_GLITCH) return FK_GLITCH;
  if (t >= T_WORN && t <= T_WORN_2) return FK_WORN;
  if (t == T_CEIL || t == T_CEIL_1) return FK_CEIL;
  if (t == T_GLASS) return FK_GLASS;
  if (t == T_ICE || t == T_ICE_1) return FK_ICE;
  return FK_NONE;
}
uint8_t eg_frag_kind_of(uint8_t t) EG_BANKED { return frag_kind_of(t); }

uint8_t eg_step_off(eg_room *r, uint8_t cell, uint8_t who, uint8_t weight) EG_BANKED {
  uint8_t t = r->c[cell], k = frag_kind_of(t), stage;
  if (k == FK_NONE || !eg_frag[k].steps || !(eg_frag[k].breakby & who)) return 0u;
  stage = (uint8_t)(t - eg_frag[k].base + weight);
  if (stage >= eg_frag[k].steps) {
    r->c[cell] = T_PIT;
    return 1u;
  }
  r->c[cell] = (uint8_t)(eg_frag[k].base + stage);
  return 0u;
}

uint8_t eg_frag_hit(eg_room *r, uint8_t cell, uint8_t who) EG_BANKED {
  uint8_t k = frag_kind_of(r->c[cell]);
  if (k == FK_NONE || EG_PROT(r, cell) || !(eg_frag[k].breakby & who)) return 0u;
  r->c[cell] = T_PIT;
  return 1u;
}

/* The flood runs in slices (eg_dead_step) so the cartridge can spread it over frames: a bit table for the seen
 * set (a variable shift is a loop on the SM83), the queue in WRAM, the key scan in chunks. */
static uint8_t dd_seen[EG_N / 8u], dd_q[EG_N];
static uint8_t dd_n, dd_h, dd_pass, dd_rock, dd_from, dd_k;
static const uint8_t dd_bit[8] = {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u};
#define DD_SEEN(c) (dd_seen[(uint8_t)(c) >> 3] & dd_bit[(uint8_t)(c) & 7u])
#define DD_MARK(c) (dd_seen[(uint8_t)(c) >> 3] |= dd_bit[(uint8_t)(c) & 7u])

static void dd_flood_begin(void) {
  uint8_t i;
  for (i = 0u; i != (uint8_t)(EG_N / 8u); i++) dd_seen[i] = 0u;
  dd_n = dd_h = 0u;
  dd_q[dd_n++] = dd_from;
  DD_MARK(dd_from);
}

static void dd_visit(const eg_room *r, uint8_t s) {
  uint8_t t;
  if (DD_SEEN(s)) return;
  t = (uint8_t)(r->c[s] & T_MASK);
  if (t == T_WALL || t == T_SOCKET || t == T_SOCKET_ON || (t == T_PIT && !dd_rock)) return;
  DD_MARK(s);
  if (t == T_ROCK) dd_rock = 1u;
  if (t != T_DOOR) dd_q[dd_n++] = s; /* the door is a goal, not a way through */
}

void eg_dead_begin(uint8_t from) EG_BANKED {
  dd_from = from;
  dd_pass = dd_rock = dd_k = 0u;
  dd_flood_begin();
}

uint8_t eg_dead_step(const eg_room *r, const uint8_t *pocket, uint8_t npocket, uint8_t budget) EG_BANKED {
  uint8_t i, k, j, p;
  /* pass 0 floods without crossing pits; if it reached a CRT, pass 1 floods again letting pits through */
  while (dd_h != dd_n) {
    if (!budget) return EG_DEAD_BUSY;
    budget--;
    i = dd_q[dd_h++];
    if (i >= EG_W) dd_visit(r, (uint8_t)(i - EG_W));
    if (i < (uint8_t)(EG_N - EG_W)) dd_visit(r, (uint8_t)(i + EG_W));
    if (i & 15u) dd_visit(r, (uint8_t)(i - 1u));
    if ((i & 15u) != 15u) dd_visit(r, (uint8_t)(i + 1u));
  }
  if (dd_rock && !dd_pass) {
    dd_pass = 1u;
    dd_flood_begin();
    return EG_DEAD_BUSY;
  }
  if (!DD_SEEN(r->door)) return EG_DEAD_DOOR;
  /* the keycards, 56 cells a slice */
  while (dd_k != (uint8_t)EG_N) {
    if (!budget) return EG_DEAD_BUSY;
    budget--;
    j = (uint8_t)(dd_k + 56u);
    do {
      if ((r->c[dd_k] & T_MASK) == T_KEY && !DD_SEEN(dd_k)) return EG_DEAD_KEY;
    } while (++dd_k != j);
  }
  /* a piece the room needs (not a herring) out of reach, with no twin in reach and none in the pocket */
  for (k = 0u; k != (uint8_t)(r->nitem - r->herrings); k++) {
    i = r->item_cell[k];
    if ((r->c[i] & T_MASK) != T_ITEM || DD_SEEN(i)) continue;
    p = r->item_piece[k];
    for (j = 0u; j != npocket; j++)
      if (pocket[j] == p) break;
    if (j != npocket) continue;
    for (j = 0u; j != r->nitem; j++)
      if (r->item_piece[j] == p && (r->c[r->item_cell[j]] & T_MASK) == T_ITEM && DD_SEEN(r->item_cell[j])) break;
    if (j == r->nitem) return EG_DEAD_ITEM;
  }
  return 0u;
}

uint8_t eg_dead(const eg_room *r, uint8_t from, const uint8_t *pocket, uint8_t npocket) EG_BANKED {
  uint8_t k;
  eg_dead_begin(from);
  while ((k = eg_dead_step(r, pocket, npocket, 255u)) == EG_DEAD_BUSY) {
  }
  return k;
}

uint8_t eg_count(const eg_room *r, uint8_t type) EG_BANKED {
  uint8_t i = 0u, n = 0u;
  do {
    if ((r->c[i] & T_MASK) == type) n++;
  } while (++i != (uint8_t)EG_N);
  return n;
}

uint8_t eg_item_at(const eg_room *r, uint8_t cell) EG_BANKED {
  uint8_t k;
  for (k = 0u; k != r->nitem; k++)
    if (r->item_cell[k] == cell) return r->item_piece[k];
  return 0u;
}
