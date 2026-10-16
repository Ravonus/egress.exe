/* Grid helpers, the changed-cell render queue, which card a cell shows, puffs. Banked (B2: ROM0 is for interrupts
 * and hot paths); the helpers every floor module calls many times a frame stay NONBANKED, in bank 0. fdx/fdy are in
 * main.c: data other banks read can't live in a switched bank. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "look.h"
#include "puzzle.h"
extern uint8_t floor_has_crt;

#pragma bank 255

uint8_t meta_tile[M_COUNT][4], meta_pal[M_COUNT];
uint16_t people[ART_SKINS + ART_HAIRS + ART_HOODIES + ART_TROUSERS];
uint8_t cracked[EG_N / 8u];
puff_t puffs[MAXP];

uint8_t cell_at(uint8_t x, uint8_t y) NONBANKED { return (uint8_t)((y & 0xf0u) | (x >> 4)); }

/* a cell coordinate's centre in subpixels, (c * 16 + 8) * 16, written as a byte shift: SDCC 4.5.1 drops the high
 * byte of the multiply-and-shift form */
uint16_t cell_sub(uint8_t c) NONBANKED { return (uint16_t)(((uint16_t)c << 8) | 0x80u); }

uint8_t is_cracked(uint8_t i) NONBANKED { return (uint8_t)(cracked[i >> 3] & (uint8_t)(1u << (i & 7u))); }

static uint8_t solid_player(uint8_t i) NONBANKED {
  uint8_t t = cell_type(i);
  if (t != T_ROCK && (room.c[i] & F_OB)) return 1u;
  return (uint8_t)(t == T_DOOR ? !door_open : !EG_PASSABLE(t)); /* B1: water, shutters, paper, sockets, pits too */
}
uint8_t box_free(uint8_t x, uint8_t y) NONBANKED {
  if((uint8_t)(x-5u)>246u || (uint8_t)(y-5u)>214u) return 0u;
  uint8_t l = (uint8_t)(x - 5u), r = (uint8_t)(x + 4u), t = (uint8_t)(y - 5u), b = (uint8_t)(y + 4u);
  return (uint8_t)(!solid_player(cell_at(l, t)) && !solid_player(cell_at(r, t)) && !solid_player(cell_at(l, b)) &&
                   !solid_player(cell_at(r, b)));
}
uint8_t mite_free(uint8_t x, uint8_t y) NONBANKED {
  if((uint8_t)(x-5u)>246u || (uint8_t)(y-5u)>214u) return 0u;
  uint8_t l = (uint8_t)(x - 5u), r = (uint8_t)(x + 4u), t = (uint8_t)(y - 5u), b = (uint8_t)(y + 4u);
  return (uint8_t)(room.c[cell_at(l, t)] == T_EMPTY && room.c[cell_at(r, t)] == T_EMPTY &&
                   room.c[cell_at(l, b)] == T_EMPTY && room.c[cell_at(r, b)] == T_EMPTY);
}
static uint8_t eye_block(uint8_t i) NONBANKED {
  uint8_t t = cell_type(i);
  return (uint8_t)(t == T_WALL || t == T_DOOR);
}
uint8_t eye_free(uint8_t x, uint8_t y) NONBANKED {
  if((uint8_t)(x-5u)>246u || (uint8_t)(y-5u)>214u) return 0u;
  uint8_t l = (uint8_t)(x - 5u), r = (uint8_t)(x + 4u), t = (uint8_t)(y - 5u), b = (uint8_t)(y + 4u);
  return (uint8_t)(!eye_block(cell_at(l, t)) && !eye_block(cell_at(r, t)) && !eye_block(cell_at(l, b)) &&
                   !eye_block(cell_at(r, b)));
}
uint8_t absdiff(uint8_t a, uint8_t b) NONBANKED { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }
uint8_t overlap(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by, uint8_t rx, uint8_t ry) NONBANKED {
  return (uint8_t)(absdiff(ax, bx) < rx && absdiff(ay, by) < ry);
}
uint8_t overlap_world(int16_t ax, int16_t ay, uint8_t bx, uint8_t by, uint8_t rx, uint8_t ry) NONBANKED {
  int16_t dx,dy;
  if((uint16_t)ax<=255u && (uint16_t)ay<=255u)
    return overlap((uint8_t)ax,(uint8_t)ay,bx,by,rx,ry);
  dx=ax-bx;dy=ay-by;
  return (uint8_t)(dx>-(int16_t)rx && dx<(int16_t)rx && dy>-(int16_t)ry && dy<(int16_t)ry);
}

uint8_t enemy_in_cell(uint8_t c) NONBANKED {
  uint8_t k;
  for (k = 0; k != nen; k++)
    if (en[k].st != ES_DEAD && cell_at((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4)) == c) return 1u;
  return 0u;
}

/* --- the changed-cell queue, drawn a few cells a frame --- */
static uint8_t cq[64], cq_head, cq_tail; /* queue_cell (hot) stays in bank 0 */
static uint8_t carpet_ink[7]; /* one bit per deterministic 2x2 carpet patch */
void cells_reset(void) BANKED {
  uint8_t x,y;
  room_origin_x=bg_origin_x=0u;room_width=EG_W;
  cq_head = cq_tail = 0;
  for(y=0u;y!=7u;y++) {
    carpet_ink[y]=0u;
    for(x=0u;x!=8u;x++) if(((x*3u+y*5u+(uint8_t)floor_seed)&7u)<2u) carpet_ink[y]|=look_bits[x];
  }
}
void grid_shift_view(int8_t direction) BANKED {
  uint8_t n=(uint8_t)(cq_head-cq_tail),k,keep=0u,i,x,v;
  for(k=0u;k<n;k++) {
    i=cq[(uint8_t)(cq_tail+k)&63u];x=EG_X(i);
    if(direction==1 ? x<4u : x>=12u)continue;
    cq[(uint8_t)(cq_tail+keep++)&63u]=(uint8_t)(direction==1 ? i-4u : i+4u);
  }
  cq_head=(uint8_t)(cq_tail+keep);
  for(k=0u;k<7u;k++) {
    v=carpet_ink[k];carpet_ink[k]=direction==1 ? (uint8_t)((v>>2)|(v<<6)) : (uint8_t)((v<<2)|(v>>6));
  }
  bg_origin_x=(uint8_t)((bg_origin_x+(direction==1 ? 8u : 24u))&31u);
}
static uint8_t carpet_patch(uint8_t i) NONBANKED {
  return (uint8_t)(room.c[i]==T_EMPTY && (carpet_ink[EG_Y(i)>>1]&look_bits[EG_X(i)>>1]));
}
static void enqueue(uint8_t i) NONBANKED {
  cq[cq_head & 63u] = i;
  cq_head++;
  if ((uint8_t)(cq_head - cq_tail) > 64u) cq_tail = (uint8_t)(cq_head - 64u);
}
void queue_cell(uint8_t i) NONBANKED {
  if(cell_type(i)==T_ROCK)floor_has_crt=1u;
  enqueue(i);
  /* A removed crate or a new gap also changes the adjoining carpet's edge. */
  if(EG_Y(i) && carpet_patch((uint8_t)(i-EG_W))) enqueue((uint8_t)(i-EG_W));
  if(EG_Y(i)<EG_H-1u && carpet_patch((uint8_t)(i+EG_W))) enqueue((uint8_t)(i+EG_W));
  if(EG_X(i) && carpet_patch((uint8_t)(i-1u))) enqueue((uint8_t)(i-1u));
  if(EG_X(i)<EG_W-1u && carpet_patch((uint8_t)(i+1u))) enqueue((uint8_t)(i+1u));
}

/* --- the look (src/look.h): which card a cell shows --- */
/* the room's outer ring of wall is the void the floor floats in */
/* (near helpers inside this bank: a BANKED call per drawn cell would cost scan lines every frame) */
static uint8_t void_ring(uint8_t i) {
  uint8_t x = EG_X(i), y = EG_Y(i);
  uint8_t t=cell_type(i);
  if(room_width>EG_W) return EG_ABYSS(room.c[i]);
  return (uint8_t)((t==T_WALL || t==T_PIT) && (x == 0u || x == EG_W - 1u || y == EG_H - 1u));
}
uint8_t look_void_cell(uint8_t i) BANKED { return void_ring(i); }
static uint8_t solid_ground(uint8_t i) { return (uint8_t)(look_here(i) && !void_ring(i)); }

static const uint8_t slab_variant[256]={
  M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,
  M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,
  M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,
  M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,
  M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,
  M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,
  M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,
  M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,
  M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,
  M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,
  M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,
  M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,
  M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,
  M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,
  M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,M_SLAB_C,M_SLAB_D,M_SLAB_D,M_SLAB_D,
  M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_A,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_B,M_SLAB_C,M_SLAB_C,
};
static uint8_t meta_for(uint8_t i) {
  uint8_t t = cell_type(i), x = EG_X(i), y = EG_Y(i), b;
  uint8_t gx=(uint8_t)(x+room_origin_x);
  uint8_t h = (uint8_t)(gx * 7u + y * 13u + (gx ^ y) + (uint8_t)floor_seed);
  if (!solid_ground(i)) {
    /* the void: under ground the slab's dithered underside, beside it the slab's side face (B2: the left card, or
     * the same tiles X-flipped for ground on the right), the two together in an inside corner, and at an outside
     * corner the side of the underside's lip. Only real neighbours: nothing glows. */
    b = (uint8_t)(y && solid_ground((uint8_t)(i - EG_W)));
    if (x && solid_ground((uint8_t)(i - 1u))) return b ? M_VOID_CORNER : M_VOID_SIDE;
    if (x != EG_W - 1u && solid_ground((uint8_t)(i + 1u))) return b ? M_VOID_CORNER_R : M_VOID_SIDE_R;
    if (b) return M_VOID_UNDER;
    if (y) {
      if (x && solid_ground((uint8_t)(i - EG_W - 1u))) return M_VOID_TIP;
      if (x != EG_W - 1u && solid_ground((uint8_t)(i - EG_W + 1u))) return M_VOID_TIP_R;
    }
    return (h & 2u) ? M_VOID_C : ((h & 1u) ? M_VOID_B : M_VOID_A);
  }
  if (look_skin[i]) return (uint8_t)(look_skin[i] - 1u); /* a fragile stage or a pit (src/look.h) */
  if((room.portal_need&EG_PORTAL_ON) && (i==room.portal[0] || i==room.portal[1]) &&
     (t==T_EMPTY || t==T_PLATE)) return puzzle_meta(i);
  if (t >= T_CRUMBLE) return puzzle_meta(i);         /* B1 puzzle cells: their cards (src/puzzle.c) */
  switch (t) {
    case T_EMPTY:
      if(carpet_patch(i)) {
        b=0u;
        if(y && carpet_patch((uint8_t)(i-EG_W))) b|=1u;
        if(x<EG_W-1u && carpet_patch((uint8_t)(i+1u))) b|=2u;
        if(y<EG_H-1u && carpet_patch((uint8_t)(i+EG_W))) b|=4u;
        if(x && carpet_patch((uint8_t)(i-1u))) b|=8u;
        return (uint8_t)(M_AUTOCARPET0+b);
      }
      return slab_variant[h]; /* exact previous h % 20 distribution */
    case T_DIRT:
      return (h & 2u) ? M_FOAM_A : M_FOAM_B;
    case T_WALL:
      if (y < EG_H - 1u) {
        b = (uint8_t)(i + EG_W);
        if ((cell_type(b) != T_WALL && cell_type(b) != T_DOOR) || void_ring(b)) {
          h = (uint8_t)(h % 9u);
          return h == 2u ? M_WALL_VENT : (h == 5u ? M_WALL_NOTE : (h == 7u ? M_WALL_SWITCH : M_WALL_FRONT));
        }
      }
      return M_WALL_TOP;
    case T_BRICK:
      return is_cracked(i) ? M_DRY_CRACK : M_DRY;
    case T_ROCK:
      return (x & 1u) ? M_CRT_A : M_CRT_B;
    case T_TOKEN:
      return M_TOKEN;
    case T_KEY:
      return M_KEY;
    default:
      return door_open ? M_DOOR_OPEN : M_DOOR_SHUT;
  }
}
uint8_t cell_meta(uint8_t i) BANKED { return meta_for(i); }

static uint8_t *quad_dst;
static const uint8_t *quad_src;
static uint8_t quad_attr;
/* The map always receives one 2x2 card. Two-byte row bursts fit even the
 * shortest remaining HBlank plus mode 2; recheck STAT before each row. The
 * caller masks interrupts only for this bounded transfer, preserving audio. */
static void quad_write(void) NONBANKED NAKED {
  __asm
    ld a,#1
    ldh (_VBK_REG),a
    ld hl,#_quad_dst
    ld a,(hl+)
    ld h,(hl)
    ld l,a
  1$:
    ldh a,(_STAT_REG)
    and a,#2
    jr nz,1$
    ld a,(#_quad_attr)
    ld (hl+),a
    ld (hl),a
    ld bc,#31
    add hl,bc
  2$:
    ldh a,(_STAT_REG)
    and a,#2
    jr nz,2$
    ld a,(#_quad_attr)
    ld (hl+),a
    ld (hl),a
    xor a,a
    ldh (_VBK_REG),a
    ld hl,#_quad_src
    ld a,(hl+)
    ld d,(hl)
    ld e,a
    ld hl,#_quad_dst
    ld a,(hl+)
    ld h,(hl)
    ld l,a
  3$:
    ldh a,(_STAT_REG)
    and a,#2
    jr nz,3$
    ld a,(de)
    inc de
    ld (hl+),a
    ld a,(de)
    inc de
    ld (hl),a
    ld bc,#31
    add hl,bc
  4$:
    ldh a,(_STAT_REG)
    and a,#2
    jr nz,4$
    ld a,(de)
    inc de
    ld (hl+),a
    ld a,(de)
    ld (hl),a
    ret
  __endasm;
}
static void meta_draw(uint8_t i, uint8_t m) {
  uint8_t p = meta_pal[m];
  /* the attribute: the palette, and VRAM bank 1 unless the card lives in bank 0 (the fragile floors, the flying cards,
   * the items, the void's edges; these may be X-flipped: LOOK_META_FLIP is the attribute's own bit) */
  quad_attr=(p & LOOK_META_BANK0) ? (uint8_t)(p & (7u | LOOK_META_FLIP)) : (uint8_t)(p | 0x08u);
  quad_dst=(uint8_t *)((LCDC_REG&LCDCF_BG9C00 ? 0x9C00u : 0x9800u)+((uint16_t)EG_Y(i)<<6)+
    (((EG_X(i)<<1)+bg_origin_x)&31u));
  quad_src=meta_tile[m];
  CRITICAL {quad_write();}
}

void draw_meta(uint8_t i, uint8_t m) BANKED { meta_draw(i, m); }
void draw_cell(uint8_t i) BANKED { meta_draw(i, meta_for(i)); }

void flush_cells(uint8_t max) BANKED {
  uint8_t i;
  while (cq_tail != cq_head && max--) {
    i = cq[cq_tail & 63u];
    meta_draw(i, meta_for(i));
    cq_tail++;
  }
}

void puff(uint8_t x, uint8_t y, uint8_t pal, uint8_t tile) BANKED {
  uint8_t i;
  for (i = 0; i != MAXP; i++)
    if (!puffs[i].t) {
      puffs[i].t = 14u;
      puffs[i].x = x;
      puffs[i].y = y;
      puffs[i].pal = pal;
      puffs[i].tile = tile;
      return;
    }
}
