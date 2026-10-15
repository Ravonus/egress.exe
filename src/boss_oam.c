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

#include "oam.h"
uint8_t bo_refused; /* mirrored from the shared allocator for the boss harness */
#define bo_group oam_group
#define bo_fits oam_fits

/* a 16-wide mirrored pair: one 8x16 tile, the right half flipped */
static uint8_t bo_pair(int16_t sx, int16_t sy, uint8_t tile, uint8_t prop) {
  if (!oam_fits(sy, 2u)) return 0;
  return (uint8_t)(oam_group(sx, sy, 1u, tile, 0, prop) &&
      oam_group(sx + 8, sy, 1u, tile, 0, (uint8_t)(prop ^ S_FLIPX)));
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
      oam_refused = (uint8_t)(oam_refused + 8u);
      return;
    }
    bo_group(sx, sy, 4, top, 2, pal);
    bo_group(sx, (int16_t)(sy + 16), 4, bot, 2, pal);
  }
}
