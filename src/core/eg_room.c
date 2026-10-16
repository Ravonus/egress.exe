/* EGRESS room gravity, pushes and rotten carpet (see eg_room.h; the generator is eg_gen.c). Shared verbatim by the cartridge and the host tools. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_room.h"
#include "eg_piece.h"

uint16_t eg_rs = 1u;
uint8_t eg_dirty[EG_DIRTY_MAX];
uint8_t eg_ndirty;

/* near versions for this file (on the cartridge the public ones are banked calls) */
static uint8_t rnd_next(void) EG_RNG_ABI {
  uint16_t x = eg_rs;
  x ^= x << 7;
  x ^= x >> 9;
  x ^= x << 8;
  eg_rs = x;
  return (uint8_t)(x ^ (x >> 8));
}

uint8_t eg_rnd(void) EG_RNG_ABI { return rnd_next(); }
uint8_t eg_rndn(uint8_t n) EG_RNG_ABI { return (uint8_t)(((uint16_t)rnd_next() * n) >> 8); }

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
static const uint8_t *scan_occ;
static uint8_t scan_n;
/* A bottom-up step never creates a CRT in a row still to be processed. Build
 * the row mask once, avoiding repeated C loop setup for twelve empty rows. */
static uint16_t rock_rows(void) __naked {
  __asm
    ld hl, #_scan_p
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld de, #0
    ld b, #12
  1$:
    sla e
    rl d
    /* Fourteen fixed cells: avoid a counter/branch on every empty cell.
     * OR the row bit for each CRT; a plate/falling flag still counts. */
    .rept 14
      ld a, (hl+)
      and a, #0x5f
      cp a, #0x04
      jr nz, .+4
      set 0, e
    .endm
    inc hl
    inc hl
    dec b
    jp nz, 1$
    ld b, d
    ld c, e
    ret
  __endasm;
}
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
/* Reject a room without shutters before the much heavier weighted-plate pass.
 * The portable pass below remains the reference, including occupied gates. */
static uint8_t scan_gates(void) __naked {
  __asm
    ld hl, #_scan_p
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld c, #224
  1$:
    ld a, (hl+)
    sub a, #11
    cp a, #2
    jr c, 2$
    dec c
    jr nz, 1$
    xor a, a
    ret
  2$:
    ld a, #1
    ret
  __endasm;
}
typedef char native_gate_values[(T_GATE==11u && T_GATE_OPEN==12u) ? 1 : -1];
/* Every plate must carry weight. A bare plate settles that result immediately;
 * checking occupancy only for bare plates avoids a general C scan per cell. */
static uint8_t scan_weights(void) __naked {
  __asm
    ld hl,#_scan_p
    ld a,(hl+)
    ld h,(hl)
    ld l,a
    ld b,#0
    ld c,#224
    ld d,#0
  1$:
    ld a,(hl+)
    cp a,#22
    jr z,2$
    cp a,#138
    jr z,2$
    cp a,#10
    jr z,4$
    and a,#0x3f
    cp a,#36
    jr nz,3$
  2$:
    set 0,d
  3$:
    inc b
    dec c
    jr nz,1$
    ld a,d
    ret
  4$:
    push hl
    push bc
    ld a,(#_scan_n)
    or a,a
    jr z,6$
    ld c,a
    ld hl,#_scan_occ
    ld a,(hl+)
    ld h,(hl)
    ld l,a
  5$:
    ld a,(hl+)
    cp a,b
    jr z,7$
    dec c
    jr nz,5$
  6$:
    pop bc
    pop hl
    ld a,#2
    ret
  7$:
    pop bc
    pop hl
    jr 2$
  __endasm;
}
typedef char native_weight_values[(T_BLOCK_PLATE==22u && T_PLATE==10u && F_OB==128u && T_ROCK==4u && F_PLATE==32u) ? 1 : -1];
static void clear_moved(void) __naked { /* EG_N cells from scan_p: F_MOVED off */
  __asm
    ld hl, #_scan_p
    ld a, (hl+)
    ld h, (hl)
    ld l, a
    ld c, #28
  1$:
    .rept 8
    ld a, (hl)
    and a, #0xbf
    ld (hl+), a
    .endm
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

static uint8_t shutters(eg_room *r, const uint8_t *occ, uint8_t nocc);

void eg_island_edges(eg_room *r) EG_BANKED {
  uint8_t y,i;
  for(y=0u;y!=EG_H;y++) {
    i=EG_IDX(0u,y);if(r->c[i]==T_WALL) r->c[i]=T_PIT|F_ABYSS;
    i=EG_IDX(EG_W-1u,y);if(r->c[i]==T_WALL) r->c[i]=T_PIT|F_ABYSS;
  }
  for(i=EG_IDX(1u,EG_H-1u);i!=EG_N-1u;i++) if(r->c[i]==T_WALL) r->c[i]=T_PIT|F_ABYSS;
}

uint8_t eg_gravity(eg_room *r, const uint8_t *occ, uint8_t nocc) EG_BANKED {
  uint8_t *c = r->c;
  uint8_t res = 0u, y, x, i, v, b, s;
#ifdef EG_GRAV_ASM
  uint16_t rows;
  scan_p = &c[EG_IDX(1u, 1u)];
  rows = rock_rows();
#endif
  eg_ndirty = 0u;
#ifdef EG_GRAV_ASM
  /* Empty physics rows are common in combat stages. The row scan already
   * proved that no CRT can move, land or strike; do not walk twelve empty
   * rows in C. Plates still follow the full grounded footprint. */
  if (!rows) return shutters(r, occ, nocc);
#endif
  /* bottom-up, so a stack of CRTs falls together; F_MOVED keeps a CRT from moving twice in one step */
  for (y = (uint8_t)(EG_H - 2u); y != 0u; y--) {
#ifdef EG_GRAV_ASM
    v = (uint8_t)(rows & 1u);
    rows >>= 1;
    if (!v) continue;
#endif
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
      if (EG_ROCK_OPEN(c[b]) || c[b] == T_PIT || EG_ABYSS(c[b])) {
        if (is_occ(b, occ, nocc)) {
          if (v & F_FALL) res |= EG_G_HIT | EG_G_CHANGED;
          c[i] = (uint8_t)(v & (uint8_t)~F_FALL);
          goto next;
        }
        if(EG_ABYSS(c[b])) {
          if(v&F_PLATE) eg_plates_dirty=1u;
          c[i]=ROCK_LEFT(v);mark(i);res|=EG_G_CHANGED|EG_G_FALLING;goto next;
        }
        move_rock(c, i, b);
        if(r->portal_need&EG_PORTAL_ON) eg_portal_object(r,b,occ,nocc);
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
          if(r->portal_need&EG_PORTAL_ON) eg_portal_object(r,s,occ,nocc);
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
  /* F_MOVED is private to this gravity step: only move_rock writes it, and
   * every such call sets FALLING. Stable boards have no transient bits to
   * clear. Keep the host's unconditional pass as the reference. */
  if (res & EG_G_FALLING) {
    scan_p = c;
    clear_moved();
  }
#else
  for (i = 0u; i != (uint8_t)EG_N; i++) c[i] &= (uint8_t)~F_MOVED;
#endif
  return (uint8_t)(res | shutters(r, occ, nocc));
}

static uint8_t shutters(eg_room *r, const uint8_t *occ, uint8_t nocc) {
  uint8_t *c = r->c;
  uint8_t res = 0u, i, v, w, gates;
  if(r->nlink) return eg_circuits(r,occ,nocc);
  /* shutters follow the plates: up while every plate carries a weight (a CRT, a laid brick or crate, an obstacle: B2;
   * B1 rooms hold one plate), down otherwise (a shutter doesn't drop on the player, nor on an obstacle: F_OB) */
#ifdef EG_GRAV_ASM
  if (!eg_plates_dirty) return res; /* no CRT came onto or off a plate: the shutters stay as they are */
  eg_plates_dirty = 0u;
  scan_p=c;
  if(!scan_gates()) return res;
  scan_occ=occ;scan_n=nocc;v=scan_weights();w=(v&2u) ? 0u : (uint8_t)(v&1u);gates=1u;
#else
  w = 0u;
  gates = 0u;
  for (i = 0u; i != (uint8_t)EG_N; i++) {
    v = c[i];
    if ((uint8_t)(v & (T_MASK | F_PLATE)) == (T_ROCK | F_PLATE) || v == T_BLOCK_PLATE || v == (T_PLATE | F_OB) || (v==T_PLATE && is_occ(i,occ,nocc))) {
      if (!(w & 2u)) w = 1u;
    }
    if (v == T_PLATE && !is_occ(i,occ,nocc)) w = 2u; /* a bare plate: every shutter stays down */
    if (v == T_GATE || v == T_GATE_OPEN) gates = 1u;
  }
  if (w == 2u) w = 0u;
#endif
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

uint8_t eg_shutters(eg_room *r, const uint8_t *occ, uint8_t nocc) EG_BANKED {
  eg_ndirty = 0;
  return shutters(r, occ, nocc);
}

uint8_t eg_push(eg_room *r, uint8_t rock, uint8_t to) EG_BANKED {
  uint8_t t;
  if(rock>=EG_N || to>=EG_N || EG_Y(rock)!=EG_Y(to) ||
     (rock+1u!=to && to+1u!=rock) || (r->c[rock]&T_MASK)!=T_ROCK) return 0u;
  t=r->c[to];
  if(EG_ABYSS(t)) {if(r->c[rock]&F_PLATE) eg_plates_dirty=1u;r->c[rock]=ROCK_LEFT(r->c[rock]);return 1u;}
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
uint8_t eg_push_block(eg_room *r, uint8_t from, uint8_t to) EG_BANKED {
  uint8_t t,f;
  if(from>=EG_N || to>=EG_N) return 0u;
  if(EG_Y(from)==EG_Y(to)) {if(from+1u!=to && to+1u!=from) return 0u;}
  else if(from+EG_W!=to && to+EG_W!=from) return 0u;
  t=r->c[to];f=r->c[from];
  if (!EG_CRATE(f)) return 0u;
  if (t == T_GLASS || t == T_CEIL || t == T_CEIL_1)
    t = T_PIT; /* through it, into the void: both gone */
  else if (t == T_EMPTY)
    t = T_BLOCK;
  else if (t == T_PLATE)
    t = T_BLOCK_PLATE;
  else if(t!=T_PIT && !EG_ABYSS(t))
    return 0u;
  r->c[to] = t;
  r->c[from] = f == T_BLOCK_PLATE ? T_PLATE : T_EMPTY;
  if (f == T_BLOCK_PLATE || t == T_BLOCK_PLATE) eg_plates_dirty = 1u;
  return 1u;
}


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
static uint8_t dd_n, dd_h, dd_pass, dd_rock, dd_from, dd_k, dd_mode, dd_locked, dd_scan, dd_flags;
static uint8_t dd_heat;
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
  if (EG_ABYSS(r->c[s]) || t == T_WALL || t == T_SOCKET || t == T_SOCKET_ON || (t==T_PAPER && !dd_heat) || (t == T_PIT && !dd_rock) ||
      (dd_locked && (t==T_GATE || t==T_GATE_OPEN))) return;
  DD_MARK(s);
  if (t == T_ROCK) dd_rock = 1u;
  if (t != T_DOOR) dd_q[dd_n++] = s; /* the door is a goal, not a way through */
}

void eg_dead_begin(uint8_t from) EG_BANKED {
  dd_from = from;
  dd_pass = dd_rock = dd_k = dd_mode = dd_locked = 0u;
  dd_scan=dd_flags=0u;
}

/* Reverse-push reachability is optimistic: other crates, breakable material and
 * closed gates do not obstruct the proof. Concrete and the map edge do. If even
 * this graph cannot move a remaining load onto any bare plate, the gates cannot
 * be opened by a crate. The same queue/bitset is reused, a bounded slice at a time. */
static uint8_t dd_permanent(const eg_room *r,int16_t x,int16_t y) {
  return (uint8_t)((uint16_t)x>=EG_W || (uint16_t)y>=EG_H || EG_ABYSS(r->c[EG_IDX(x,y)]) ||
                   (r->c[EG_IDX(x,y)]&T_MASK)==T_WALL);
}
static void dd_weight_cell(const eg_room *r,uint8_t i) {
    uint8_t t=(uint8_t)(r->c[i]&T_MASK);
    if(t==T_ROCK || t==T_BLOCK_PLATE) dd_flags|=1u;
    if(t==T_PLATE) {
      dd_flags|=2u;DD_MARK(i);dd_q[dd_n++]=i;
      /* A neighbouring gate may be held by the player while leaving the plate. */
      if((i>=EG_W && (r->c[i-EG_W]&T_MASK)==T_GATE) ||
         (i<EG_N-EG_W && (r->c[i+EG_W]&T_MASK)==T_GATE) ||
         (EG_X(i) && (r->c[i-1u]&T_MASK)==T_GATE) ||
         (EG_X(i)<15u && (r->c[i+1u]&T_MASK)==T_GATE)) dd_flags|=1u;
    }
    if(t==T_GATE || t==T_GATE_OPEN) dd_flags|=4u;
}
static uint8_t dd_weight_tools(const eg_room *r,const uint8_t *pocket,uint8_t n) {
  uint8_t i,t,p,tools=0u;
  for(i=0u;i<(uint8_t)(n+r->nitem);i++) {
    if(i<n) p=pocket[i];
    else {t=(uint8_t)(i-n);if((r->c[r->item_cell[t]]&T_MASK)!=T_ITEM) continue;p=r->item_piece[t];}
    if(p==P_BRICK) return 1u;
    if(p==P_FIRE) tools|=1u;if(p==P_MUD) tools|=2u;
    if(p==P_EARTH) tools|=4u;if(p==P_WATER) tools|=8u;
  }
  return (uint8_t)((tools&1u) && ((tools&2u) || (tools&12u)==12u));
}
static void dd_jump(const eg_room *r,uint8_t i,int8_t dx,int8_t dy) {
  int16_t x=(int16_t)EG_X(i)+dx,y=(int16_t)EG_Y(i)+dy;uint8_t t;
  if((uint16_t)x>=EG_W || (uint16_t)y>=EG_H) return;
  t=(uint8_t)(r->c[EG_IDX(x,y)]&T_MASK);if(t!=T_PIT && t!=T_GLITCH) return;
  x+=dx;y+=dy;if((uint16_t)x<EG_W && (uint16_t)y<EG_H) dd_visit(r,EG_IDX(x,y));
}
uint8_t eg_dead_step(const eg_room *r, const uint8_t *pocket, uint8_t npocket, uint8_t budget) EG_BANKED {
  uint8_t i, k, j, p, bridge = 0u;
  if(!budget) return EG_DEAD_BUSY;
  if(!dd_mode) {
    dd_mode=3u;dd_scan=dd_flags=dd_n=dd_h=0u;
    dd_heat=0u;
    for(i=0u;i<npocket;i++)if(EG_PIECE_HOT(pocket[i]))dd_heat=1u;
    for(i=0u;i<r->nitem;i++)if((r->c[r->item_cell[i]]&T_MASK)==T_ITEM && EG_PIECE_HOT(r->item_piece[i]))dd_heat=1u;
    for(i=0u;i!=EG_N/8u;i++) dd_seen[i]=0u;
    /* Separate circuits and portals need their dependency proof; the cheap
     * geometric hint must not apply the legacy all-plates reverse-push rule. */
    if(r->nob || r->nlink || (r->portal_need&EG_PORTAL_ON)) {dd_mode=2u;dd_flood_begin();}
  }
  if(dd_mode==3u) {
    /* Initialization is sliced too: at most four cell probes per budget unit,
     * reusing the reverse queue as the plate list instead of scanning twice. */
    while(dd_scan!=EG_N && budget && !(dd_flags&1u)) {
      budget--;
      for(k=0u;k!=4u && dd_scan!=EG_N;k++) dd_weight_cell(r,dd_scan++);
    }
    if(dd_scan==EG_N || (dd_flags&1u)) {
      if((dd_flags&1u) || !(dd_flags&2u) || !(dd_flags&4u) || dd_weight_tools(r,pocket,npocket)) {
        dd_mode=2u;dd_flood_begin();
      } else dd_mode=1u;
    }
    return EG_DEAD_BUSY;
  }
  if(dd_mode==1u) {
    static const int8_t dx[4]={0,1,0,-1},dy[4]={-1,0,1,0};
    if(budget>1u) budget>>=1; /* reverse pushes test two signed cells in each direction */
    while(dd_h!=dd_n && budget) {
      int16_t x,y,px,py;budget--;i=dd_q[dd_h++];
      if(EG_CRATE(r->c[i])) {dd_locked=0u;break;}
      for(k=0u;k!=4u;k++) {
        x=(int16_t)EG_X(i)-dx[k];y=(int16_t)EG_Y(i)-dy[k];px=x-dx[k];py=y-dy[k];
        if(dd_permanent(r,x,y) || dd_permanent(r,px,py)) continue;
        j=EG_IDX(x,y);if(!DD_SEEN(j)) {DD_MARK(j);dd_q[dd_n++]=j;}
      }
    }
    if(dd_h==dd_n || (dd_h && EG_CRATE(r->c[dd_q[dd_h-1u]]))) {
      dd_locked=(uint8_t)(!dd_h || !EG_CRATE(r->c[dd_q[dd_h-1u]]));
      dd_mode=2u;dd_flood_begin();
    }
    return EG_DEAD_BUSY;
  }
  if(dd_mode==4u) goto check_keys;
  /* pass 0 floods without crossing wide pits; short roll-jumps remain possible. */
  while (dd_h != dd_n) {
    if (!budget) return EG_DEAD_BUSY;
    budget--;
    i = dd_q[dd_h++];
    if(r->portal_need&EG_PORTAL_ON) {
      if(i==r->portal[0] && r->portal[1]<EG_N) dd_visit(r,r->portal[1]);
      if(i==r->portal[1] && r->portal[0]<EG_N) dd_visit(r,r->portal[0]);
    }
    if (i >= EG_W) dd_visit(r, (uint8_t)(i - EG_W));
    if (i < (uint8_t)(EG_N - EG_W)) dd_visit(r, (uint8_t)(i + EG_W));
    if (i & 15u) dd_visit(r, (uint8_t)(i - 1u));
    if ((i & 15u) != 15u) dd_visit(r, (uint8_t)(i + 1u));
    dd_jump(r,i,0,-1);dd_jump(r,i,1,0);dd_jump(r,i,0,1);dd_jump(r,i,-1,0);
  }
  if (!dd_pass && !dd_rock) {
    /* An optimistic reachability hint, not the solver: reachable mud (or its ingredients)
     * can cross a gap too. Never announce a lost room while that route remains. */
    for (i = 0u; i != npocket; i++) {
      p = pocket[i];
      if (p == P_MUD) bridge |= 4u;
      if (p == P_EARTH) bridge |= 1u;
      if (p == P_WATER) bridge |= 2u;
    }
    for (i = 0u; i != r->nitem; i++) {
      if (!DD_SEEN(r->item_cell[i]) || (r->c[r->item_cell[i]]&T_MASK) != T_ITEM) continue;
      p = r->item_piece[i];
      if (p == P_MUD) bridge |= 4u;
      if (p == P_EARTH) bridge |= 1u;
      if (p == P_WATER) bridge |= 2u;
    }
    if ((bridge & 4u) || (bridge & 3u) == 3u) dd_rock = 1u;
  }
  if (dd_rock && !dd_pass) {
    dd_pass = 1u;
    dd_flood_begin();
    return EG_DEAD_BUSY;
  }
  if (!DD_SEEN(r->door)) return EG_DEAD_DOOR;
  dd_mode=4u;
  return EG_DEAD_BUSY;
check_keys:
  /* the keycards, 56 cells a slice */
  if (dd_k != (uint8_t)EG_N) {
    j = (uint8_t)(dd_k + 56u);
    do {
      if ((r->c[dd_k] & T_MASK) == T_KEY && !DD_SEEN(dd_k)) return EG_DEAD_KEY;
    } while (++dd_k != j);
    return EG_DEAD_BUSY;
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
