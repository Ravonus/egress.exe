/* Native mob rendering: separate from the AI/rule bank so neither must discard
 * states or art to fit a 16 KiB ROM bank. The look cache is shared WRAM. */
#pragma bank 255
#include <gb/gb.h>
#include "play.h"
#include "mob.h"
#include "oam.h"
#include "gen/mob_art.h"
extern uint8_t mob_slot_bmask[MOB_SLOTS],mob_has_elite;

/* ---- drawing ---- */
static const int8_t bob2[4] = {0, -1, 0, 0};

void mob_draw(uint8_t k) BANKED {
  enemy_t *e = &en[k];
  mob_t *m = &mob[k];
  uint8_t f, pal, tile, ex = (uint8_t)(e->x >> 4), ey = (uint8_t)(e->y >> 4), flip;
  int16_t sx = (int16_t)ex - 16 - cam_x, sy = (int16_t)ey - 16 - cam_y;
  m->drawn = 0;
  if (m->status & MS_UNDER) { /* a bump in the carpet, moving */
    if (e->anim & 8u) oam_group(sx + 4, sy + 6, 1u, (uint8_t)(MOB_FX_TILE + FXT_BUMP * 2u), 0, 5u);
    return;
  }
  f = (uint8_t)((e->anim >> 4) & 1u);
  if (e->st == ES_WALK || e->st == ES_FLOAT) f = (uint8_t)((e->anim >> 3) & 1u);
  pal = (uint8_t)(2u + (m->slot & 1u));
  if (e->st == ES_TELL) {
    f = 2u;
    sx += (e->t & 2u) ? 1 : -1;
    if (e->t & 4u) pal = 7u;
  } else if (e->st == ES_LUNGE) {
    sx += fdx[e->face] * 2;
    sy += fdy[e->face] * 2;
  }
  if (m->ax.body == MB_FLYER) sy += bob2[(e->anim >> 3) & 3u];
  if ((m->flags & MOBF_ELITE) && (e->anim & 0x38u) == 0x38u) pal = 7u; /* the elite's pulse */
  if (m->status & MS_FROZEN) pal = 5u;
  if (e->flash) pal = (e->flash & 2u) ? 6u : pal;
  tile = (uint8_t)(MOB_TILE0 + m->slot * MOB_SLOT_TILES + (f << 2));
  flip = (uint8_t)(e->face == F_LEFT);
  if (flip) m->drawn = oam_group(sx, sy, 2u, (uint8_t)(tile + 2u), 254u, (uint8_t)(pal | S_FLIPX));
  else m->drawn = oam_group(sx, sy, 2u, tile, 2u, pal);
  if (m->drawn) m->drawn = (uint8_t)(f + 1u);
}

/* OP_MARK: elite crowns (bobbing 1 px), shield rings; OP_OVER: overlays of bodies placed this frame */
void mobs_draw(uint8_t pass) BANKED {
  uint8_t k, f, b, tile;
  int16_t sx, sy;
  if(pass==OP_MARK && !mob_has_elite) return;
  for (k = 0; k != nen; k++) {
    enemy_t *e = &en[k];
    mob_t *m = &mob[k];
    if (e->kind != SPAWN_MOB || e->st == ES_DEAD || (m->status & MS_UNDER)) continue;
    if (pass == OP_MARK) {
      if (m->flags & MOBF_ELITE) {
        sx = (int16_t)(e->x >> 4) - 16 - cam_x;
        sy = (int16_t)(e->y >> 4) - 16 - cam_y;
        oam_group(sx + 4, sy - 9 + ((e->anim >> 4) & 1u), 1u, (uint8_t)(MOB_FX_TILE + FXT_CROWN * 2u), 0, 7u);
      }
      continue;
    }
    if (!m->drawn || m->drawn > 3u) continue;
    f = (uint8_t)(m->drawn - 1u);
    if (f == 2u) f = 1u;
    b = (uint8_t)((mob_slot_bmask[m->slot] >> (f << 1)) & 3u);
    if (!b) continue;
    sx = (int16_t)(e->x >> 4) - 16 - cam_x;
    sy = (int16_t)(e->y >> 4) - 16 - cam_y;
    if (e->st == ES_TELL) sx += (e->t & 2u) ? 1 : -1;
    else if (e->st == ES_LUNGE) {
      sx += fdx[e->face] * 2;
      sy += fdy[e->face] * 2;
    }
    if (m->ax.body == MB_FLYER) sy += bob2[(e->anim >> 3) & 3u];
    tile = (uint8_t)(MOB_TILE0 + m->slot * MOB_SLOT_TILES + 16u + (f << 2));
    if(b==3u) { /* A complete two-column overlay shares one reservation. */
      if(e->face==F_LEFT) oam_group(sx,sy,2u,(uint8_t)(tile+2u),254u,5u|S_FLIPX);
      else oam_group(sx,sy,2u,tile,2u,5u);
      continue;
    }
    if (e->face == F_LEFT) { /* flipped: the right column draws on the left */
      if (b & 2u) oam_group(sx, sy, 1u, (uint8_t)(tile + 2u), 0, 5u | S_FLIPX);
      if (b & 1u) oam_group(sx + 8, sy, 1u, tile, 0, 5u | S_FLIPX);
    } else {
      if (b & 1u) oam_group(sx, sy, 1u, tile, 0, 5u);
      if (b & 2u) oam_group(sx + 8, sy, 1u, (uint8_t)(tile + 2u), 0, 5u);
    }
  }
}

uint8_t mob_look_draw(uint8_t slot, uint8_t f, int16_t sx, int16_t sy, uint8_t prop_or) BANKED {
  return oam_group(sx, sy, 2u, (uint8_t)(MOB_TILE0 + slot * MOB_SLOT_TILES + (f << 2)), 2u,
                   (uint8_t)((2u + (slot & 1u)) | prop_or));
}

