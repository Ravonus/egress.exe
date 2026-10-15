/* A boss floor's sprites, never more than 10 on a scan line (docs/redesign.md 5.5; the owner's direction: spawn
 * spacing plus a rotating flicker, the heir, the weapon and the boss's markers first).
 *
 * The same rules as b2-mobs' src/oam.h, so the two merge into one: every OBJ is counted in 4-line bands of OAM y (an
 * 8x16 OBJ at OAM y `oy` covers lines oy .. oy+15: 4 or 5 bands), and a group of OBJs that must show together is
 * placed all or nothing, only if no band would pass 10. Priority is drawing order:
 *   the heir, the weapon (arc, charge spark), the boss's markers (slam shadows, the beam's column, the ram's lane,
 *   the heart shard), the boss's body (the stamps and the beam, or LAST FAX), shots (orbs), summoned mobs from a
 *   rotating start, particles, and last the look's flying cards (rotating: a card that can't show this frame shows
 *   next frame).
 * The Tenant's body is background, so the arena's sprites stay light: the heir 4 + the weapon 2 + a stamp 2 + a beam
 * or a shadow 2 is 10 at most where they meet, by construction (a stamp never hangs on the row it slams).
 *
 * boss_draw_marks and boss_draw_body are the boss's two classes for b2-mobs' draw_sprites (OP_MARK, OP_BOSS): at
 * integration bo_group becomes oam_group and boss_draw_sprites goes. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "boss_int.h"
#include "heir.h"
#include "look.h"

#define LINE_MAX 10u
#define BANDS 42u
static uint8_t band[BANDS], n, cap, last_n, was_busy, spin;
uint8_t bo_refused; /* harness: OBJs refused this frame (they flicker) */

/* the bands an OBJ group at screen top `sy` covers (clipped); 0 if wholly off */
static uint8_t span(int16_t sy, uint8_t *lo, uint8_t *hi) {
  int16_t oy = sy + 16, a = oy, b = oy + 15;
  if (b < 0 || a > 167) return 0;
  if (a < 0) a = 0;
  if (b > 167) b = 167;
  *lo = (uint8_t)((uint8_t)a >> 2);
  *hi = (uint8_t)((uint8_t)b >> 2);
  return 1u;
}
static uint8_t bo_fits(int16_t sy, uint8_t cnt) {
  uint8_t lo, hi;
  if (!span(sy, &lo, &hi)) return 1u;
  for (; lo <= hi; lo++)
    if ((uint8_t)(band[lo] + cnt) > LINE_MAX) return 0;
  return 1u;
}
static void bo_add(int16_t sy, uint8_t cnt) {
  uint8_t lo, hi;
  if (!span(sy, &lo, &hi)) return;
  for (; lo <= hi; lo++) band[lo] = (uint8_t)(band[lo] + cnt);
}
static void put(int16_t sx, int16_t sy, uint8_t tile, uint8_t prop) {
  volatile OAM_item_t *o = &shadow_OAM[n++];
  o->y = (uint8_t)(sy + 16);
  o->x = (uint8_t)(sx + 8);
  o->tile = tile;
  o->prop = prop;
}
static uint8_t visible(int16_t sx, int16_t sy) { return (uint8_t)(sx > -8 && sx < 160 && sy > -16 && sy < 144); }

/* `cnt` 8x16 OBJs side by side from screen (sx, sy), tiles tile0, tile0 + tstep, ...: all or nothing */
static uint8_t bo_group(int16_t sx, int16_t sy, uint8_t cnt, uint8_t tile0, uint8_t tstep, uint8_t prop) {
  uint8_t k, v = 0;
  for (k = 0; k != cnt; k++)
    if (visible((int16_t)(sx + (int16_t)(k << 3)), sy)) v++;
  if (!v) return 1u;
  if ((uint8_t)(n + v) > cap || !bo_fits(sy, v)) {
    bo_refused = (uint8_t)(bo_refused + v);
    return 0;
  }
  for (k = 0; k != cnt; k++, tile0 = (uint8_t)(tile0 + tstep))
    if (visible((int16_t)(sx + (int16_t)(k << 3)), sy)) put((int16_t)(sx + (int16_t)(k << 3)), sy, tile0, prop);
  bo_add(sy, v);
  return 1u;
}
/* a 16-wide mirrored pair: one 8x16 tile, the right half flipped */
static uint8_t bo_pair(int16_t sx, int16_t sy, uint8_t tile, uint8_t prop) {
  uint8_t v = (uint8_t)(visible(sx, sy) + visible((int16_t)(sx + 8), sy));
  if (!v) return 1u;
  if ((uint8_t)(n + v) > cap || !bo_fits(sy, v)) {
    bo_refused = (uint8_t)(bo_refused + v);
    return 0;
  }
  if (visible(sx, sy)) put(sx, sy, tile, prop);
  if (visible((int16_t)(sx + 8), sy)) put((int16_t)(sx + 8), sy, tile, (uint8_t)(prop ^ S_FLIPX));
  bo_add(sy, v);
  return 1u;
}

#define SX(wx) ((int16_t)(wx) - (int16_t)cam_x)
#define SY(wy) ((int16_t)(wy) - (int16_t)cam_y)

/* --- the boss's markers: what is about to hit, where --- */
void boss_draw_marks(void) BANKED {
  uint8_t h, k;
  int16_t y;
  if (bs_st == BS_SHARD) { /* the heart shard, bobbing */
    y = SY(bs_shard_y) - 8 - (int16_t)((frame_count >> 3) & 1u);
    bo_group(SX(bs_shard_x) - 4, y, 1, bs_kind == BOSS_BIOME ? BA_T_SHARD : BA_F_SHARD, 2, 5);
    return;
  }
  if (bs_st != BS_ATTACK && bs_st != BS_GATE) return;
  if (bs_kind == BOSS_BIOME) {
    for (h = 0; h != 2u; h++) /* a slam's shadow: it follows, then holds; it blinks faster as the drop nears */
      if (bs_hs[h] == HS_RISE || bs_hs[h] == HS_DROP) {
        if (bs_hs[h] == HS_RISE && bs_ht[h] < 12u && (bs_ht[h] & 2u)) continue;
        bo_pair(SX(bs_htx[h]) - 8, SY(bs_hty[h]) - 2, BA_T_SHADOW, 7);
      }
    if (tenant_beam() == 1u && (frame_count & 4u)) /* the column the beam will burn */
      for (y = 100; y < 176; y += 24) bo_group(SX(bs_tx) - 4, SY(y), 1, BA_T_MARK, 2, 7);
  } else if (bs_atk == ATK_RAM && bs_hs[0] == HS_RISE && (frame_count & 4u)) { /* the ram's lane */
    for (k = 1; k != 3u; k++) {
      int16_t mx = (int16_t)bs_x + (int16_t)bs_vx * (int16_t)(k * 8u) - 4, my = (int16_t)bs_y + (int16_t)bs_vy * (int16_t)(k * 8u) - 8;
      if (bs_vx) bo_group(SX(mx), SY(my), 1, BA_F_CHEV, 2, (uint8_t)(7u | (bs_vx < 0 ? S_FLIPX : 0u)));
      else bo_group(SX(mx), SY(my), 1, BA_F_CHEVD, 2, (uint8_t)(7u | (bs_vy < 0 ? S_FLIPY : 0u)));
    }
  }
}

/* --- the boss itself --- */
void boss_draw_body(void) BANKED {
  uint8_t h, t, pal, st;
  int16_t y;
  if (bs_st == BS_DEAD || en[0].st == ES_DEAD) return;
  if (bs_kind == BOSS_BIOME) {
    if (bs_st == BS_INTRO) return;
    for (h = 0; h != 2u; h++) {
      st = bs_hs[h];
      t = BA_T_STAMP_IDLE;
      pal = 3;
      if (st == HS_RISE || st == HS_DROP || st == HS_PREP || st == HS_SWEEP) {
        t = BA_T_STAMP_LIFT;
        if ((st == HS_RISE || st == HS_PREP) && (bs_flash & 4u)) pal = 7;
      } else if (st == HS_REST)
        t = bs_harm ? BA_T_STAMP_ARMOUR : BA_T_STAMP_SLAM;
      if (st == HS_REST && en[0].flash && bs_open && (en[0].x >> 4) == bs_hx[h]) pal = (en[0].flash & 2u) ? 6u : pal;
      y = SY(bs_hy[h]) - 8 + ((st == HS_HOME && ((frame_count >> 4) & 1u)) ? 1 : 0);
      if (bs_st == BS_DOWN && bs_t < 60u) y += (int16_t)(60u - bs_t); /* they drop away */
      bo_group(SX(bs_hx[h]) - 8, y, 2, t, 2, pal);
    }
    if (tenant_beam() == 2u)
      for (y = 96; y < 176; y += 16) bo_pair(SX(bs_tx) - 8, SY(y), BA_T_BEAM, 5);
    return;
  }
  /* LAST FAX: 32x32, two rows of four */
  if (bs_st == BS_DOWN && (bs_t & 2u)) return; /* it flickers out */
  {
    uint8_t top = BA_F_IDLE, bot = BA_F_IDLE + 8u;
    int16_t sx = SX(bs_x) - 16, sy = SY(bs_y) - 16;
    pal = 3;
    if (bs_st == BS_ATTACK && (bs_hs[0] == HS_RISE || bs_hs[0] == HS_SWEEP)) {
      top = BA_F_TELL;
      bot = BA_F_TELL + 8u;
      if (bs_hs[0] == HS_RISE) {
        if (bs_flash & 4u) pal = 7;
        sx += (bs_t & 2u) ? 1 : -1;
      }
    } else if (bs_st == BS_ATTACK && (bs_hs[0] == HS_REST || bs_hs[0] == HS_DROP))
      top = BA_F_STUN;
    else if ((frame_count >> 4) & 1u)
      sy += 1;
    if (en[0].flash && (en[0].flash & 2u)) pal = 6;
    if (!bo_fits(sy, 4) || !bo_fits((int16_t)(sy + 16), 4)) {
      bo_refused = (uint8_t)(bo_refused + 8u);
      return;
    }
    bo_group(sx, sy, 4, top, 2, pal);
    bo_group(sx, (int16_t)(sy + 16), 4, bot, 2, pal);
  }
}

static const int8_t bob[8] = {0, 0, -1, -1, 0, 0, 1, 1};

/* the whole boss floor (until b2-mobs' draw_sprites and oam.h land) */
void boss_draw_sprites(void) BANKED {
  uint8_t k, j, busy = look_busy();
  int16_t x, y;
  memset(band, 0, sizeof band);
  n = 0;
  bo_refused = 0;
  spin++;
  cap = busy ? 24u : 40u;
  if (busy && !was_busy)
    for (k = 24; k != 40u; k++) shadow_OAM[k].y = 0; /* mine from last frame: the look takes these from now */
  /* the heir */
  heir_tick();
  if (!pl_hp || !(pl_inv && (pl_inv & 4u)) || pl_state == PS_HURT || ha_state == HA_SPAWN) {
    j = n;
    n = (uint8_t)(n + heir_draw(n, (int16_t)pl_x - 8 - cam_x, (int16_t)pl_y - 15 - cam_y + 8));
    for (k = j; k != n; k++) bo_add((int16_t)shadow_OAM[k].y - 16, 1);
  }
  /* the weapon */
  if (pl_arc == 1u) {
    uint8_t reach = (mods & MOD_LONG) ? 16u : 13u, fr = (uint8_t)(pl_t >= 5u ? 4u : 0u), t;
    x = (int16_t)pl_x - 8 + fdx[pl_face] * (int8_t)reach;
    y = (int16_t)pl_y - 8 + fdy[pl_face] * (int8_t)reach;
    t = pl_face == F_UP ? ST_ARC_UP : (pl_face == F_DOWN ? ST_ARC_DOWN : ST_ARC_SIDE);
    if (pl_face == F_LEFT) bo_group(SX(x) - 8, SY(y) - 8, 2, (uint8_t)(t + fr + 2u), (uint8_t)-2, 4u | S_FLIPX);
    else bo_group(SX(x) - 8, SY(y) - 8, 2, (uint8_t)(t + fr), 2, 4);
  } else if (pl_arc == 2u) {
    k = (uint8_t)((pl_t >> 1) & 3u);
    x = (int16_t)pl_x - 8 + fdx[k] * 14;
    y = (int16_t)pl_y - 8 + fdy[k] * 14;
    j = (uint8_t)((k == F_UP ? ST_ARC_UP : (k == F_DOWN ? ST_ARC_DOWN : ST_ARC_SIDE)) + 4u);
    if (k == F_LEFT) bo_group(SX(x) - 8, SY(y) - 8, 2, (uint8_t)(j + 2u), (uint8_t)-2, 4u | S_FLIPX);
    else bo_group(SX(x) - 8, SY(y) - 8, 2, j, 2, 4);
  }
  if (pl_charge >= CHARGE_FULL && (frame_count & 4u))
    bo_group(SX((int16_t)pl_x - 4) - 4, SY((int16_t)pl_y - 22) - 4, 1, ST_SPARK, 2, 4);
  /* the boss */
  boss_draw_marks();
  boss_draw_body();
  /* shots */
  for (k = 0; k != MAXO; k++)
    if (orbs[k].live)
      bo_group(SX((int16_t)(orbs[k].x >> 4) - 4) - 4, SY((int16_t)(orbs[k].y >> 4) - 4) - 4, 1, ST_ORB, 2,
               orbs[k].friendly ? 4u : 5u);
  /* summoned mobs, from a rotating start */
  for (j = 0; j != MAXE - 1u; j++) {
    enemy_t *e;
    uint8_t base, pal;
    k = (uint8_t)(1u + (uint8_t)(j + spin) % (MAXE - 1u));
    if (k >= nen) continue;
    e = &en[k];
    if (e->st == ES_DEAD || e->kind == SPAWN_BOSS) continue;
    x = (int16_t)(e->x >> 4) - 8;
    y = (int16_t)(e->y >> 4) - 8;
    if (e->kind == SPAWN_MITE) {
      base = (uint8_t)(ST_MITE + (((e->anim >> 3) & 1u) << 2));
      pal = 2;
      if (e->st == ES_TELL || e->st == ES_LUNGE) {
        base = ST_MITE + 8u;
        if (e->st == ES_TELL) pal = (e->t & 4u) ? 7u : 2u;
      }
    } else {
      base = ST_EYE;
      pal = 3;
      y += bob[(e->anim >> 3) & 7u];
      if (e->st == ES_TELL) {
        base = ST_EYE + 4u;
        pal = (e->t & 4u) ? 7u : 3u;
      }
    }
    if (e->flash && (e->flash & 2u)) pal = 6;
    bo_group(SX(x) - 8, SY(y) - 8, 2, base, 2, pal);
  }
  /* particles: dropped first */
  {
    puff_t *p = puffs;
    for (k = MAXP; k; k--, p++)
      if (p->t) {
        p->t--;
        y = (int16_t)p->y - 4 - (int16_t)((14u - p->t) >> 2);
        bo_group(SX((int16_t)p->x - 4) - 4, SY(y) - 4, 1, p->tile, 2, p->pal);
      }
  }
  /* the look's flying cards (slots 24..39 in pairs), from a rotating start: hide what doesn't fit this frame */
  if (busy && was_busy)
    for (j = 0; j != 8u; j++) {
      uint8_t a = (uint8_t)(39u - (((j + spin) & 7u) << 1)), b = (uint8_t)(a - 1u);
      uint8_t ya = shadow_OAM[a].y, yb = shadow_OAM[b].y, c = (uint8_t)((ya ? 1u : 0u) + (yb ? 1u : 0u));
      if (!c) continue;
      y = (int16_t)(ya ? ya : yb) - 16;
      if (bo_fits(y, c)) bo_add(y, c);
      else {
        shadow_OAM[a].y = shadow_OAM[b].y = 0;
        bo_refused = (uint8_t)(bo_refused + c);
      }
    }
  for (k = n; k < last_n; k++) shadow_OAM[k].y = 0;
  if (!busy)
    for (k = n; k != 40u; k++) shadow_OAM[k].y = 0;
  last_n = n;
  was_busy = busy;
}
