/* Drawing the floor's sprites into shadow OAM (8x16 mode): the heir in two layers (BODY palette over WEAR palette),
 * the lead's arcs, enemies, the boss (32x32), orbs, particles and puffs.
 *
 * B2: every OBJ goes through oam_group (src/oam.h), which keeps each scan line at 10 or fewer, and drawing order is
 * priority (heir, weapon, markers, boss, shots, mobs from a rotating start, overlays, particles). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "heir.h"
#include "look.h"
#include "oam.h"
#include "mob.h"

uint8_t oam_n, oam_cap = 40u, oam_spin, oam_refused;
static uint8_t oam_last;
static uint8_t band[OAM_BANDS + 2u];

void sprites_reset(void) BANKED {
  uint8_t i;
  for (i = 0; i != 40u; i++) move_sprite(i, 0, 0);
  oam_last = 0;
  heir_reset(); /* the floor's art was reloaded over the heir's tiles */
  mobs_reload(); /* B2: and over the mobs' looks */
}

/* --- the per-line budget (src/oam.h) --- */
/* bands touched by an 8x16 OBJ at OAM y oy: oy>>2 .. (oy+15)>>2 */
static uint8_t fits(uint8_t oy, uint8_t n) {
  uint8_t *p = &band[oy >> 2], *e = &band[(uint8_t)(oy + 15u) >> 2], lim = (uint8_t)(OAM_LINE_MAX - n);
  for (;;) {
    if (*p > lim) return 0;
    if (p == e) return 1u;
    p++;
  }
}
static void take(uint8_t oy, uint8_t n) {
  uint8_t *p = &band[oy >> 2], *e = &band[(uint8_t)(oy + 15u) >> 2];
  for (;;) {
    *p += n;
    if (p == e) return;
    p++;
  }
}

uint8_t oam_fits(int16_t sy, uint8_t n) BANKED {
  if (sy < -15 || sy > 143) return 1u;
  return fits((uint8_t)(sy + 16), n);
}

uint8_t oam_group(int16_t sx, int16_t sy, uint8_t n, uint8_t tile0, uint8_t tstep, uint8_t prop) BANKED {
  uint8_t oy, vis = 0, k, ox;
  int16_t x;
  OAM_item_t *o;
  if (sy < -15 || sy > 143) return 1u; /* off screen: nothing to place, nothing refused */
  for (k = 0, x = sx; k != n; k++, x += 8)
    if (x > -8 && x < 160) vis++;
  if (!vis) return 1u;
  oy = (uint8_t)(sy + 16);
  if ((uint8_t)(oam_n + vis) > oam_cap || !fits(oy, vis)) {
    oam_refused += vis;
    return 0;
  }
  take(oy, vis);
  o = &shadow_OAM[oam_n];
  oam_n += vis;
  for (k = 0, x = sx; k != n; k++, x += 8, tile0 += tstep) {
    if (x <= -8 || x >= 160) continue;
    ox = (uint8_t)(x + 8);
    o->y = oy;
    o->x = ox;
    o->tile = tile0;
    o->prop = prop;
    o++;
  }
  return 1u;
}

void oam_count(uint8_t from) BANKED {
  OAM_item_t *o = &shadow_OAM[from];
  for (; from != oam_n; from++, o++)
    if (o->y) take(o->y, 1u);
}

uint8_t oam_band_max(void) BANKED {
  uint8_t i, m = 0;
  for (i = 0; i != OAM_BANDS; i++)
    if (band[i] > m) m = band[i];
  return m;
}

/* a 16x16 pair: tiles tile, tile+2 (flipped: the right tile first) */
static uint8_t spr16(int16_t wx, int16_t wy, uint8_t tile, uint8_t pal, uint8_t flip) {
  int16_t sx = wx - cam_x - 8, sy = wy - cam_y - 8;
  if (flip) return oam_group(sx, sy, 2u, (uint8_t)(tile + 2u), 254u, (uint8_t)(pal | S_FLIPX));
  return oam_group(sx, sy, 2u, tile, 2u, pal);
}
static uint8_t spr8(int16_t wx, int16_t wy, uint8_t tile, uint8_t pal) {
  return oam_group(wx - cam_x - 4, wy - cam_y - 4, 1u, tile, 0, pal);
}
/* 32x32: two rows of four, each row all or nothing */
static void spr32(int16_t wx, int16_t wy, uint8_t tile, uint8_t pal) {
  int16_t sx = wx - cam_x, sy = wy - cam_y;
  oam_group(sx, sy, 4u, tile, 2u, pal);
  oam_group(sx, sy + 16, 4u, (uint8_t)(tile + 8u), 2u, pal);
}

static const int8_t bob[8] = {0, 0, -1, -1, 0, 0, 1, 1};

/* the B1 kinds: mite, eye, the Watchman (boss.c) */
static void draw_b1(enemy_t *e) {
  uint8_t base, pal, flip;
  int16_t x, y;
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

static void draw_boss(enemy_t *e) {
  uint8_t base = ST_BOSS, pal = 3;
  int16_t y = (int16_t)(e->y >> 4) - 16 + bob[(e->anim >> 3) & 7u], x = (int16_t)(e->x >> 4) - 16;
  if (e->st == ES_TELL) {
    base = ST_BOSS + 16u;
    pal = (e->t & 4u) ? 7u : 3u;
    x += (e->t & 2u) ? 1 : -1;
  } else if (e->st == ES_STUN)
    base = ST_BOSS + 32u;
  if (e->flash) pal = (e->flash & 2u) ? 6u : pal;
  spr32(x, y, base, pal);
}

void draw_sprites(void) BANKED {
  uint8_t k, fr, start;
  int16_t x, y;
  oam_n = 0;
  oam_refused = 0;
  oam_cap = look_busy_n ? 24u : 40u;
  oam_spin++;
  {
    uint8_t *p = band;
    for (k = OAM_BANDS + 2u; k; k--) *p++ = 0;
  }
  /* OP_HEIR: the heir (heir.c), BODY layer over WEAR layer, its box 16x22 with the feet at pl_y + 6 */
  heir_tick();
  if (!pl_hp || !(pl_inv && (pl_inv & 4u)) || pl_state == PS_HURT || ha_state == HA_SPAWN) {
    oam_n = (uint8_t)(oam_n + heir_draw(oam_n, (int16_t)pl_x - 8 - cam_x, (int16_t)pl_y - 15 - cam_y + 8));
    oam_count(0);
  }
  /* OP_WEAPON: an arc toward the cardinal facing while a strike is out, four around during the spin */
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
  /* OP_MARK: elite crowns and the like (mob.c) */
  if (nen) mobs_draw(OP_MARK);
  /* OP_BOSS */
  for (k = 0; k != nen; k++)
    if (en[k].kind == SPAWN_BOSS && en[k].st != ES_DEAD) draw_boss(&en[k]);
  /* OP_SHOT: orbs (pointers, not orbs[k]: SDCC multiplies the index on every access) */
  {
    orb_t *o = orbs;
    for (k = MAXO; k; k--, o++)
      if (o->live) spr8((int16_t)(o->x >> 4) - 4, (int16_t)(o->y >> 4) - 4, ST_ORB, o->friendly ? 4u : 5u);
  }
  /* OP_MOB: bodies, from a rotating start so a crowded line flickers fairly */
  if (nen) {
    start = (uint8_t)(oam_spin % nen);
    k = start;
    do {
      enemy_t *e = &en[k];
      if (e->st != ES_DEAD) {
        if (e->kind == SPAWN_MOB) mob_draw(k);
        else if (e->kind != SPAWN_BOSS) draw_b1(e);
      }
      if (++k == nen) k = 0;
    } while (k != start);
    /* OP_OVER */
    mobs_draw(OP_OVER);
  }
  /* OP_FX: particles, then puffs (cosmetic: left out where a line is full) */
  fx_draw();
  {
    puff_t *p = puffs;
    int16_t py;
    for (k = MAXP; k; k--, p++)
      if (p->t) {
        p->t--;
        py = (int16_t)p->y - 4 - (int16_t)((14u - p->t) >> 2);
        spr8((int16_t)p->x - 4, py, p->tile, p->pal);
      }
  }
  for (k = oam_n; k < oam_last; k++) shadow_OAM[k].y = 0;
  oam_last = oam_n;
}
