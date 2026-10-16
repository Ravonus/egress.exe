/* Drawing the floor's sprites into shadow OAM (8x16 mode): the heir in two layers (BODY palette over WEAR palette),
 * the lead's arcs, enemies, the boss (32x32), orbs, particles and puffs.
 *
 * B2: every OBJ goes through oam_group (src/oam.h), which keeps each scan line at 10 or fewer, and drawing order is
 * priority (heir, weapon, markers, boss, shots, mobs from a rotating start, overlays, particles). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "boss_api.h" /* B2 bosses */
#include "heir.h"
#include "look.h"
#include "oam.h"
#include "mob.h"
#include "obstacle.h"
#include "core/eg_move.h"
#include "gen/diag_arc.inc"
extern eg_mover_t pmv;

uint8_t oam_n, oam_cap = 40u, oam_spin, oam_refused;
static uint8_t oam_last, was_busy;
static uint8_t band[OAM_BANDS + 2u];
static OAM_item_t *emit_dst;
static uint8_t emit_x, emit_y, emit_tile, emit_step, emit_prop, emit_n;

/* A visible group is already clipped and budgeted. Keep its four-byte writes
 * in registers: SDCC otherwise spills every member and pointer to the stack. */
static void emit_group(void) NONBANKED NAKED {
  __asm
    ld a, (#_emit_dst)
    ld l, a
    ld a, (#_emit_dst + 1)
    ld h, a
    ld a, (#_emit_n)
    ld b, a
    ld a, (#_emit_prop)
    ld c, a
    ld a, (#_emit_tile)
    ld d, a
    ld a, (#_emit_x)
    ld e, a
  1$:
    ld a, (#_emit_y)
    ld (hl+), a
    ld a, e
    ld (hl+), a
    add a, #8
    ld e, a
    ld a, d
    ld (hl+), a
    ld a, (#_emit_step)
    add a, d
    ld d, a
    ld a, c
    ld (hl+), a
    dec b
    jr nz, 1$
    ret
  __endasm;
}

void sprites_reset(void) BANKED {
  uint8_t i;
  for (i = 0; i != 40u; i++) move_sprite(i, 0, 0);
  oam_last = 0;
  VBK_REG=1u;set_sprite_data(0u,4u,diagonal_arc);VBK_REG=0u;
  heir_reset(); /* the floor's art was reloaded over the heir's tiles */
  if (!boss_floor) mobs_reload(); /* B2: and over the mobs' looks */
}

/* --- the per-line budget (src/oam.h) --- */
/* bands touched by an 8x16 OBJ at OAM y oy: oy>>2 .. (oy+15)>>2 */
static uint8_t fits(uint8_t oy, uint8_t n) NONBANKED {
  uint8_t *p = &band[oy >> 2], count = (uint8_t)(4u + ((oy & 3u) != 0u)), lim = (uint8_t)(OAM_LINE_MAX - n);
  do {
    if (*p > lim) return 0;
    p++;
  } while (--count);
  return 1u;
}
static void take(uint8_t oy, uint8_t n) NONBANKED {
  uint8_t *p = &band[oy >> 2], count = (uint8_t)(4u + ((oy & 3u) != 0u));
  do {
    *p += n;
    p++;
  } while (--count);
}

uint8_t oam_fits(int16_t sy, uint8_t n) NONBANKED {
  if ((uint16_t)(sy + 15) > 158u) return 1u;
  return fits((uint8_t)(sy + 16), n);
}

uint8_t oam_group(int16_t sx, int16_t sy, uint8_t n, uint8_t tile0, uint8_t tstep, uint8_t prop) NONBANKED {
  uint8_t oy, vis = 0, k, ox, full;
  int16_t x;
  OAM_item_t *o;
  if ((uint16_t)(sy + 15) > 158u) return 1u; /* off screen: nothing to place, nothing refused */
  full = (uint8_t)((uint16_t)sx <= (uint8_t)(160u - (n << 3)));
  if (full) vis = n;
  else for (k = 0, x = sx; k != n; k++, x += 8)
    if ((uint16_t)(x + 7) < 167u) vis++;
  if (!vis) return 1u;
  oy = (uint8_t)(sy + 16);
  if ((uint8_t)(oam_n + vis) > oam_cap || !fits(oy, vis)) {
    oam_refused += vis;
    return 0;
  }
  take(oy, vis);
  o = &shadow_OAM[oam_n];
  oam_n += vis;
  if (full) {
    emit_dst = o;
    emit_x = (uint8_t)(sx + 8);
    emit_y = oy;
    emit_tile = tile0;
    emit_step = tstep;
    emit_prop = prop;
    emit_n = n;
    emit_group();
    return 1u;
  }
  for (k = 0, x = sx; k != n; k++, x += 8, tile0 += tstep) {
    if ((uint16_t)(x + 7) >= 167u) continue;
    ox = (uint8_t)(x + 8);
    o->y = oy;
    o->x = ox;
    o->tile = tile0;
    o->prop = prop;
    o++;
  }
  return 1u;
}

void oam_count(uint8_t from) NONBANKED {
  OAM_item_t *o = &shadow_OAM[from];
  for (; from != oam_n; from++, o++)
    if (o->y && o->y < 160u) take(o->y, 1u);
}

uint8_t oam_band_max(void) NONBANKED {
  uint8_t i, m = 0;
  for (i = 0; i != OAM_BANDS; i++)
    if (band[i] > m) m = band[i];
  return m;
}

/* The look moves during hitstop and pause. Recount without advancing poses. */
void sprites_budget(void) NONBANKED {
  uint8_t k, y;
  for (k = 0; k != OAM_BANDS + 2u; k++) band[k] = 0;
  for (k = 0; k != 40u; k++) {
    y = shadow_OAM[k].y;
    if (!y || y >= 160u || !shadow_OAM[k].x || shadow_OAM[k].x >= 168u) continue;
    if (fits(y, 1u)) take(y, 1u);
    else { shadow_OAM[k].y = 0; oam_refused++; }
  }
}

static void untake_card(uint8_t k) NONBANKED {
  uint8_t oy = shadow_OAM[k].y, *p, count;
  if (!oy || oy >= 160u || !shadow_OAM[k].x || shadow_OAM[k].x >= 168u) return;
  p = &band[oy >> 2];
  count = (uint8_t)(4u + ((oy & 3u) != 0u));
  do {
    if (*p) (*p)--;
    p++;
  } while (--count);
}

void oam_card_pair(uint8_t a, uint8_t b, uint8_t x0, uint8_t x1, uint8_t y,
                   uint8_t tile, uint8_t prop) NONBANKED {
  uint8_t n = (uint8_t)((x0 && x0 < 168u) + (x1 && x1 < 168u));
  untake_card(a);
  if (b != a) untake_card(b);
  shadow_OAM[a].y = shadow_OAM[b].y = 0;
  if (!n || !y || y >= 160u) return;
  if (!fits(y, n)) { oam_refused += n; return; }
  take(y, n);
  CRITICAL {
    if (b != a) {
      shadow_OAM[b].x = x1;
      shadow_OAM[b].tile = tile + 2u;
      shadow_OAM[b].prop = prop;
      shadow_OAM[b].y = y;
    }
    shadow_OAM[a].x = x0;
    shadow_OAM[a].tile = tile;
    shadow_OAM[a].prop = prop;
    shadow_OAM[a].y = y;
  }
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
  x = (int16_t)(e->x >> 4);
  y = (int16_t)(e->y >> 4);
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
  if (look_busy_n && !was_busy)
    for (k = 24u; k != 40u; k++) shadow_OAM[k].y = 0;
  oam_spin++;
  {
    uint8_t *p = band;
    for (k = OAM_BANDS + 2u; k; k--) *p++ = 0;
  }
  /* OP_HEIR: the heir (heir.c), BODY layer over WEAR layer, its box 16x22 with the feet at pl_y + 6 */
  heir_tick();
  if ((pl_state!=PS_FALL || pl_t<24u) && (!pl_hp || !(pl_inv && (pl_inv & 4u)) || pl_state == PS_HURT || ha_state == HA_SPAWN)) {
    /* heir_draw writes hardware OAM coordinates; its X already includes the hardware's 8-pixel bias. */
    oam_n = (uint8_t)(oam_n + heir_draw(oam_n, (int16_t)pl_x-cam_x, (int16_t)pl_y-7-cam_y-pl_z));
    oam_count(0);
  }
  /* OP_WEAPON: aim and arc share all eight directions. */
  if (pl_arc == 1u) {
    uint8_t reach=heir.weapon==1u ? 12u : heir.weapon==2u ? 24u : heir.weapon==3u ? 10u : heir.weapon==4u ? 20u : 14u;
    if(mods&MOD_LONG) reach+=4u;
    fr=(uint8_t)(pl_t>=5u ? 4u : 0u);
    if(pmv.facing&1u) {
      uint8_t left=(uint8_t)(pmv.facing>=5u),down=(uint8_t)(pmv.facing==3u || pmv.facing==5u);
      reach=(uint8_t)(reach-(reach>>2));
      x=(int16_t)pl_x+(left ? -(int8_t)reach : (int8_t)reach);
      y=(int16_t)pl_y+(down ? (int8_t)reach : -(int8_t)reach);
      if(left) oam_group(x-cam_x-8,y-cam_y-8,2u,2u,254u,(uint8_t)(4u|S_BANK|S_FLIPX|(down ? S_FLIPY : 0u)));
      else oam_group(x-cam_x-8,y-cam_y-8,2u,0u,2u,(uint8_t)(4u|S_BANK|(down ? S_FLIPY : 0u)));
    } else {
      x=(int16_t)pl_x+fdx[pl_face]*(int8_t)reach;y=(int16_t)pl_y+fdy[pl_face]*(int8_t)reach;
      if(pl_face==F_UP) spr16(x,y,(uint8_t)(ST_ARC_UP+fr),4u,0u);
      else if(pl_face==F_DOWN) spr16(x,y,(uint8_t)(ST_ARC_DOWN+fr),4u,0u);
      else spr16(x,y,(uint8_t)(ST_ARC_SIDE+fr),4u,(uint8_t)(pl_face==F_LEFT));
    }
  } else if (pl_arc == 2u) {
    k = (uint8_t)((pl_t >> 1) & 3u);
    x = (int16_t)pl_x + fdx[k] * 14;
    y = (int16_t)pl_y + fdy[k] * 14;
    spr16(x, y, (uint8_t)((k == F_UP ? ST_ARC_UP : (k == F_DOWN ? ST_ARC_DOWN : ST_ARC_SIDE)) + 4u), 4,
          (uint8_t)(k == F_LEFT));
  }
  if (pl_charge >= CHARGE_FULL && (frame_count & 4u)) spr8((int16_t)pl_x, (int16_t)pl_y - 18, ST_SPARK, 4);
  /* OP_MARK: elite crowns and the like (mob.c) */
  if (boss_floor) boss_draw_marks();
  else if (nen) mobs_draw(OP_MARK);
  /* OP_BOSS */
  if (boss_floor) boss_draw_body();
  else for (k = 0; k != nen; k++)
    if (en[k].kind == SPAWN_BOSS && en[k].st != ES_DEAD) draw_boss(&en[k]);
  /* OP_SHOT: orbs (pointers, not orbs[k]: SDCC multiplies the index on every access) */
  {
    orb_t *o = orbs;
    for (k = MAXO; k; k--, o++)
      if (o->live) spr8((int16_t)(o->x >> 4), (int16_t)(o->y >> 4), ST_ORB, o->friendly ? 4u : 5u);
  }
  /* OP_MOB: bodies, from a rotating start so a crowded line flickers fairly */
  if (room.nob) obstacle_draw();
  if (nen && !(boss_floor && nen == 1u)) {
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
  if (!boss_floor) fx_draw(); /* boss art owns the mob/FX tile block in an arena */
  {
    puff_t *p = puffs;
    int16_t py;
    for (k = MAXP; k; k--, p++)
      if (p->t) {
        p->t--;
        py = (int16_t)p->y - (int16_t)((14u - p->t) >> 2);
        spr8((int16_t)p->x, py, p->tile, p->pal);
      }
  }
  if (look_busy_n && was_busy) {
    /* Flying cards occupy the high OAM slots outside the actor allocator. They
     * still share the hardware's ten-OBJ line limit with actors and shots. */
    for (k = 24u; k != 40u; k++) {
      uint8_t y = shadow_OAM[k].y;
      if (!y) continue;
      if (fits(y, 1u)) take(y, 1u);
      else { shadow_OAM[k].y = 0; oam_refused++; }
    }
  }
  for (k = oam_n; k < oam_last && k < oam_cap; k++) shadow_OAM[k].y = 0;
  if (!look_busy_n)
    for (k = oam_n; k != 40u; k++) shadow_OAM[k].y = 0;
  was_busy = look_busy_n != 0u;
  oam_last = oam_n;
  { extern uint8_t bo_refused; bo_refused = oam_refused; }
}
