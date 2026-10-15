/* Bank 0: grid helpers every floor module calls many times a frame, the changed-cell render queue, puffs. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "look.h"
#include "puzzle.h"

const int8_t fdx[4] = {0, 0, -1, 1}, fdy[4] = {-1, 1, 0, 0};
uint8_t meta_tile[M_COUNT][4], meta_pal[M_COUNT];
uint16_t people[ART_SKINS + ART_HAIRS + ART_HOODIES + ART_TROUSERS];
uint8_t cracked[EG_N / 8u];
puff_t puffs[MAXP];

uint8_t cell_at(uint8_t x, uint8_t y) NONBANKED { return (uint8_t)((y & 0xf0u) | (x >> 4)); }

/* a cell coordinate's centre in subpixels, (c * 16 + 8) * 16, written as a byte shift: SDCC 4.5.1 drops the high
 * byte of the multiply-and-shift form */
uint16_t cell_sub(uint8_t c) NONBANKED { return (uint16_t)(((uint16_t)c << 8) | 0x80u); }

uint8_t is_cracked(uint8_t i) NONBANKED { return (uint8_t)(cracked[i >> 3] & (uint8_t)(1u << (i & 7u))); }

static uint8_t solid_player(uint8_t i) {
  uint8_t t = cell_type(i);
  return (uint8_t)(t == T_DOOR ? !door_open : !EG_PASSABLE(t)); /* B1: water, shutters, paper, sockets, pits too */
}
uint8_t box_free(uint8_t x, uint8_t y) NONBANKED {
  uint8_t l = (uint8_t)(x - 5u), r = (uint8_t)(x + 4u), t = (uint8_t)(y - 5u), b = (uint8_t)(y + 4u);
  return (uint8_t)(!solid_player(cell_at(l, t)) && !solid_player(cell_at(r, t)) && !solid_player(cell_at(l, b)) &&
                   !solid_player(cell_at(r, b)));
}
uint8_t mite_free(uint8_t x, uint8_t y) NONBANKED {
  uint8_t l = (uint8_t)(x - 5u), r = (uint8_t)(x + 4u), t = (uint8_t)(y - 5u), b = (uint8_t)(y + 4u);
  return (uint8_t)(room.c[cell_at(l, t)] == T_EMPTY && room.c[cell_at(r, t)] == T_EMPTY &&
                   room.c[cell_at(l, b)] == T_EMPTY && room.c[cell_at(r, b)] == T_EMPTY);
}
static uint8_t eye_block(uint8_t i) {
  uint8_t t = cell_type(i);
  return (uint8_t)(t == T_WALL || t == T_DOOR);
}
uint8_t eye_free(uint8_t x, uint8_t y) NONBANKED {
  uint8_t l = (uint8_t)(x - 5u), r = (uint8_t)(x + 4u), t = (uint8_t)(y - 5u), b = (uint8_t)(y + 4u);
  return (uint8_t)(!eye_block(cell_at(l, t)) && !eye_block(cell_at(r, t)) && !eye_block(cell_at(l, b)) &&
                   !eye_block(cell_at(r, b)));
}
uint8_t absdiff(uint8_t a, uint8_t b) NONBANKED { return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a); }
uint8_t overlap(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by, uint8_t rx, uint8_t ry) NONBANKED {
  return (uint8_t)(absdiff(ax, bx) < rx && absdiff(ay, by) < ry);
}

uint8_t enemy_in_cell(uint8_t c) NONBANKED {
  uint8_t k;
  for (k = 0; k != nen; k++)
    if (en[k].st != ES_DEAD && cell_at((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4)) == c) return 1u;
  return 0u;
}

/* --- the changed-cell queue, drawn a few cells a frame --- */
static uint8_t cq[64], cq_head, cq_tail;
void cells_reset(void) NONBANKED { cq_head = cq_tail = 0; }
void queue_cell(uint8_t i) NONBANKED {
  cq[cq_head & 63u] = i;
  cq_head++;
  if ((uint8_t)(cq_head - cq_tail) > 64u) cq_tail = (uint8_t)(cq_head - 64u);
}

/* --- the look (src/look.h): which card a cell shows --- */
/* the room's outer ring of wall is the void the floor floats in */
uint8_t look_void_cell(uint8_t i) NONBANKED {
  uint8_t x = EG_X(i), y = EG_Y(i);
  return (uint8_t)(cell_type(i) == T_WALL && (x == 0u || x == EG_W - 1u || y == EG_H - 1u));
}
static uint8_t solid_ground(uint8_t i) { return (uint8_t)(look_here(i) && !look_void_cell(i)); }

static uint8_t meta_for(uint8_t i) {
  uint8_t t = cell_type(i), x = EG_X(i), y = EG_Y(i), b;
  uint8_t h = (uint8_t)(x * 7u + y * 13u + (x ^ y) + (uint8_t)floor_seed);
  if (!solid_ground(i)) {
    /* the void; under ground it shows the slab's dithered underside */
    if (y && solid_ground((uint8_t)(i - EG_W))) return M_VOID_UNDER;
    return (h & 2u) ? M_VOID_C : ((h & 1u) ? M_VOID_B : M_VOID_A);
  }
  if (look_skin[i]) return (uint8_t)(look_skin[i] - 1u); /* a fragile stage or a pit (src/look.h) */
  if (t >= T_CRUMBLE) return puzzle_meta(i);         /* B1 puzzle cells: their cards (src/puzzle.c) */
  switch (t) {
    case T_EMPTY:
      h = (uint8_t)(h % 20u); /* A 50%, B 20%, C 15%, D 15% */
      return h < 10u ? M_SLAB_A : (h < 14u ? M_SLAB_B : (h < 17u ? M_SLAB_C : M_SLAB_D));
    case T_DIRT:
      return (h & 2u) ? M_FOAM_A : M_FOAM_B;
    case T_WALL:
      if (y < EG_H - 1u) {
        b = (uint8_t)(i + EG_W);
        if ((cell_type(b) != T_WALL && cell_type(b) != T_DOOR) || look_void_cell(b)) {
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
uint8_t cell_meta(uint8_t i) NONBANKED { return meta_for(i); }

void draw_meta(uint8_t i, uint8_t m) NONBANKED {
  uint8_t a[4];
  uint8_t p = meta_pal[m];
  /* the attribute: the palette, and VRAM bank 1 unless the card lives in bank 0 (the fragile floors, the flying cards) */
  a[0] = a[1] = a[2] = a[3] = (p & LOOK_META_BANK0) ? (uint8_t)(p & 7u) : (uint8_t)(p | 0x08u);
  VBK_REG = 1;
  set_bkg_tiles((uint8_t)(EG_X(i) << 1), (uint8_t)(EG_Y(i) << 1), 2, 2, a);
  VBK_REG = 0;
  set_bkg_tiles((uint8_t)(EG_X(i) << 1), (uint8_t)(EG_Y(i) << 1), 2, 2, meta_tile[m]);
}

void draw_cell(uint8_t i) NONBANKED {
  if (cell_type(i) == T_ITEM && !look_skin[i] && solid_ground(i)) { /* B1: a piece shows its name (src/puzzle.c) */
    puzzle_draw_item(i);
    return;
  }
  draw_meta(i, meta_for(i));
}

void flush_cells(uint8_t max) NONBANKED {
  while (cq_tail != cq_head && max--) {
    draw_cell(cq[cq_tail & 63u]);
    cq_tail++;
  }
}

void puff(uint8_t x, uint8_t y, uint8_t pal, uint8_t tile) NONBANKED {
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
