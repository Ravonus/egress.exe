/* Drawing the floor's sprites into shadow OAM (8x16 mode): the heir in two layers (BODY palette over WEAR palette),
 * the lead's arcs, enemies, the boss (32x32), orbs and puffs. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "heir.h"

static uint8_t oam_n, oam_last;

void sprites_reset(void) BANKED {
  uint8_t i;
  for (i = 0; i != 40u; i++) move_sprite(i, 0, 0);
  oam_last = 0;
  heir_reset(); /* the floor's art was reloaded over the heir's tiles */
}

static void spr16(int16_t wx, int16_t wy, uint8_t tile, uint8_t pal, uint8_t flip) {
  int16_t sx = wx - cam_x, sy = wy - cam_y + 8;
  if (oam_n > 38u) return;
  if (sx < -8 || sx > 168 || sy < 0 || sy > 152) return;
  set_sprite_tile(oam_n, flip ? (uint8_t)(tile + 2u) : tile);
  set_sprite_tile((uint8_t)(oam_n + 1u), flip ? tile : (uint8_t)(tile + 2u));
  set_sprite_prop(oam_n, (uint8_t)(pal | (flip ? S_FLIPX : 0u)));
  set_sprite_prop((uint8_t)(oam_n + 1u), (uint8_t)(pal | (flip ? S_FLIPX : 0u)));
  move_sprite(oam_n, (uint8_t)sx, (uint8_t)sy);
  move_sprite((uint8_t)(oam_n + 1u), (uint8_t)(sx + 8), (uint8_t)sy);
  oam_n += 2u;
}
static void spr8(int16_t wx, int16_t wy, uint8_t tile, uint8_t pal) {
  int16_t sx = wx - cam_x + 4, sy = wy - cam_y + 12;
  if (oam_n > 39u) return;
  if (sx < 0 || sx > 168 || sy < 0 || sy > 152) return;
  set_sprite_tile(oam_n, tile);
  set_sprite_prop(oam_n, pal);
  move_sprite(oam_n, (uint8_t)sx, (uint8_t)sy);
  oam_n++;
}
/* room for one more 8x16 sprite at OAM y `oy` without an 11th on any of its lines (the hardware would drop it):
 * the busiest of its lines is its top line or the top line of a sprite already placed over it */
static uint8_t line_room(uint8_t oy) {
  uint8_t ys[16], n = 0, k, j, c, top;
  if (oam_n < 10u) return 1; /* fewer than 10 placed: no line can hold 10 yet */
  for (k = 0; k != oam_n; k++) {
    uint8_t y = shadow_OAM[k].y;
    if (y && (uint8_t)(y - oy + 15u) < 31u && n != 16u) ys[n++] = y; /* |y - oy| < 16: shares a line */
  }
  if (n < 10u) return 1;
  for (j = 0; j <= n; j++) {
    top = j == n ? oy : (ys[j] > oy ? ys[j] : oy); /* a candidate line, as the OAM y of a sprite starting there */
    c = 1;
    for (k = 0; k != n; k++)
      if (ys[k] <= top && (uint8_t)(top - ys[k]) < 16u) c++;
    if (c > 10u) return 0;
  }
  return 1;
}

/* 32x32: four columns of two 8x16 rows */
static void spr32(int16_t wx, int16_t wy, uint8_t tile, uint8_t pal) {
  uint8_t r, c;
  int16_t sx, sy;
  for (r = 0; r != 2u; r++)
    for (c = 0; c != 4u; c++) {
      if (oam_n > 39u) return;
      sx = wx - cam_x + 8 + (int16_t)(c * 8u);
      sy = wy - cam_y + 16 + (int16_t)(r * 16u);
      if (sx < 0 || sx > 168 || sy < 0 || sy > 160) continue;
      set_sprite_tile(oam_n, (uint8_t)(tile + ((r * 4u + c) << 1)));
      set_sprite_prop(oam_n, pal);
      move_sprite(oam_n, (uint8_t)sx, (uint8_t)sy);
      oam_n++;
    }
}

static const int8_t bob[8] = {0, 0, -1, -1, 0, 0, 1, 1};

void draw_sprites(void) BANKED {
  uint8_t k, base, fr, flip, pal;
  int16_t x, y;
  oam_n = 0;
  /* the player: the heir (heir.c), BODY layer over WEAR layer, its box 16x22 with the feet at pl_y + 6 */
  heir_tick();
  if (!pl_hp || !(pl_inv && (pl_inv & 4u)) || pl_state == PS_HURT || ha_state == HA_SPAWN)
    oam_n = (uint8_t)(oam_n + heir_draw(oam_n, (int16_t)pl_x - 8 - cam_x, (int16_t)pl_y - 15 - cam_y + 8));
  /* the lead: an arc toward the cardinal facing while a strike is out, four around during the spin */
  if (pl_arc == 1u) {
    uint8_t reach = (mods & MOD_LONG) ? 16u : 13u;
    fr = (uint8_t)(pl_t >= 5u ? 4u : 0u);
    x = (int16_t)pl_x - 8 + fdx[pl_face] * (int8_t)reach;
    y = (int16_t)pl_y - 8 + fdy[pl_face] * (int8_t)reach;
    if (pl_face == F_UP) spr16(x, y, (uint8_t)(ST_ARC_UP + fr), 4, 0);
    else if (pl_face == F_DOWN) spr16(x, y, (uint8_t)(ST_ARC_DOWN + fr), 4, 0);
    else spr16(x, y, (uint8_t)(ST_ARC_SIDE + fr), 4, (uint8_t)(pl_face == F_LEFT));
  } else if (pl_arc == 2u) {
    k = (uint8_t)((pl_t >> 1) & 3u);
    x = (int16_t)pl_x - 8 + fdx[k] * 14;
    y = (int16_t)pl_y - 8 + fdy[k] * 14;
    spr16(x, y, (uint8_t)((k == F_UP ? ST_ARC_UP : (k == F_DOWN ? ST_ARC_DOWN : ST_ARC_SIDE)) + 4u), 4,
          (uint8_t)(k == F_LEFT));
  }
  if (pl_charge >= CHARGE_FULL && (frame_count & 4u)) spr8((int16_t)pl_x - 4, (int16_t)pl_y - 22, ST_SPARK, 4);
  /* enemies */
  for (k = 0; k != nen; k++) {
    enemy_t *e = &en[k];
    if (e->st == ES_DEAD) continue;
    if (e->kind == SPAWN_BOSS) {
      base = ST_BOSS;
      pal = 3;
      y = (int16_t)(e->y >> 4) - 16 + bob[(e->anim >> 3) & 7u];
      x = (int16_t)(e->x >> 4) - 16;
      if (e->st == ES_TELL) {
        base = ST_BOSS + 16u;
        pal = (e->t & 4u) ? 7u : 3u;
        x += (e->t & 2u) ? 1 : -1;
      } else if (e->st == ES_STUN)
        base = ST_BOSS + 32u;
      if (e->flash) pal = (e->flash & 2u) ? 6u : pal;
      spr32(x, y, base, pal);
      continue;
    }
    x = (int16_t)(e->x >> 4) - 8;
    y = (int16_t)(e->y >> 4) - 8;
    if (e->kind == SPAWN_MITE) {
      base = (uint8_t)(ST_MITE + (((e->anim >> 3) & 1u) << 2));
      pal = 2;
      if (e->st == ES_TELL) {
        base = ST_MITE + 8u;
        x += (e->t & 2u) ? 1 : -1;
        pal = (e->t & 4u) ? 7u : 2u;
      } else if (e->st == ES_LUNGE)
        base = ST_MITE + 8u;
      else if (e->st == ES_REST && (e->t & 8u))
        base = ST_MITE;
      flip = (uint8_t)((e->anim >> 4) & 1u);
    } else {
      base = ST_EYE;
      pal = 3;
      y += bob[(e->anim >> 3) & 7u];
      if (e->st == ES_TELL) {
        base = ST_EYE + 4u;
        pal = (e->t & 4u) ? 7u : 3u;
      }
      flip = 0;
    }
    if (e->flash) pal = (e->flash & 2u) ? 6u : pal;
    spr16(x, y, base, pal, flip);
  }
  { /* pointers, not orbs[k] / puffs[k]: SDCC multiplies the index on every access */
    orb_t *o = orbs;
    puff_t *p = puffs;
    int16_t py;
    for (k = MAXO; k; k--, o++)
      if (o->live) spr8((int16_t)(o->x >> 4) - 4, (int16_t)(o->y >> 4) - 4, ST_ORB, o->friendly ? 4u : 5u);
    for (k = MAXP; k; k--, p++)
      if (p->t) { /* cosmetic: left out where it would be an 11th sprite on a line */
        p->t--;
        py = (int16_t)p->y - 4 - (int16_t)((14u - p->t) >> 2);
        if (!line_room((uint8_t)(py - cam_y + 12))) continue;
        spr8((int16_t)p->x - 4, py, p->tile, p->pal);
      }
  }
  for (k = oam_n; k < oam_last; k++) move_sprite(k, 0, 0);
  oam_last = oam_n;
}
