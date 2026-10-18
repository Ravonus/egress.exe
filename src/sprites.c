/* Drawing the floor's sprites into shadow OAM (8x16 mode): the heir in two layers (BODY palette over WEAR palette),
 * the lead's arcs, enemies, the boss (32x32), orbs, particles and puffs.
 *
 * B2: every OBJ goes through oam_group (src/oam.h), which keeps each scan line at 10 or fewer, and drawing order is
 * priority (heir, weapon, markers, boss, shots, mobs from a rotating start, overlays, particles). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <stddef.h>
#include "play.h"
#include "boss_api.h" /* B2 bosses */
#include "heir.h"
#include "look.h"
#include "oam.h"
#include "mob.h"
#include "obstacle.h"
#include "stage.h"
#include "world.h"
#include "core/eg_move.h"
#include "gen/diag_arc.inc"
#include "gen/hammer.inc"
#include "core/eg_input.h"
#include "companion.h"
#include "elements.h"
#include "health.h"
#include "puzzle.h"
#include "gen/mob_art.h"
extern eg_input_t pin;
extern eg_mover_t pmv;
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;

uint8_t oam_n, oam_cap = 40u, oam_spin, oam_refused;
static uint8_t oam_last, was_busy;
static uint8_t band[OAM_BANDS + 2u];
static OAM_item_t *emit_dst;
static uint8_t emit_x, emit_y, emit_tile, emit_step, emit_prop, emit_n;

/* The allocator owns a contiguous range. Clear its stale tail once, with a
 * four-byte stride, rather than recomputing an OAM pointer for every entry. */
static void hide_range(uint8_t from,uint8_t end) NONBANKED NAKED {
  from;end;
  __asm
    ld c,a
    ld a,e
    sub a,c
    ret z
    ret c
    ld b,a
    ld a,c
    add a,a
    add a,a
    add a,#<_shadow_OAM
    ld l,a
    ld h,#>_shadow_OAM
  1$:
    ld (hl),#0
    ld a,l
    add a,#4
    ld l,a
    dec b
    jr nz,1$
    ret
  __endasm;
}

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
  oam_last = was_busy = 0;
  VBK_REG=1u;set_sprite_data(0u,4u,heir.weapon==1u ? hammer_diagonal : diagonal_arc);VBK_REG=0u;
  if(heir.weapon==1u)set_sprite_data(ST_ARC_DOWN,24u,hammer_arc);
  heir_reset(); /* the floor's art was reloaded over the heir's tiles */
  if (!boss_floor) mobs_reload(); /* and over the mobs' looks */
  else mob_art_fx(MOB_FX_TILE); /* current boss art owns 158..205, not the FX atlas */
  health_load();companion_reset_position();companion_art_tick();
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

/* Validate then reserve the same four/five bands in one register-only pass.
 * Never partially reserve a group that the last band would refuse. */
static uint8_t reserve(uint8_t oy,uint8_t n) NONBANKED NAKED {
  oy;n;
  __asm
    ld d,a
    rrca
    rrca
    and a,#0x3f
    add a,#<_band
    ld l,a
    ld a,#>_band
    adc a,#0
    ld h,a
    ld a,#11
    sub a,e
    ld c,a
    ld a,(hl+)
    cp a,c
    jr nc,9$
    ld a,(hl+)
    cp a,c
    jr nc,9$
    ld a,(hl+)
    cp a,c
    jr nc,9$
    ld a,(hl+)
    cp a,c
    jr nc,9$
    ld a,d
    and a,#3
    jr z,1$
    ld a,(hl)
    cp a,c
    jr nc,9$
    add a,e
    ld (hl),a
  1$:
    dec hl
    ld a,(hl)
    add a,e
    ld (hl),a
    dec hl
    ld a,(hl)
    add a,e
    ld (hl),a
    dec hl
    ld a,(hl)
    add a,e
    ld (hl),a
    dec hl
    ld a,(hl)
    add a,e
    ld (hl),a
    ld a,#1
    ret
  9$:
    xor a,a
    ret
  __endasm;
}

uint8_t oam_fits(int16_t sy, uint8_t n) NONBANKED {
  if ((uint16_t)(sy + 15) > 158u) return 1u;
  return fits((uint8_t)(sy + 16), n);
}

static uint8_t oam_clipped(int16_t sx, int16_t sy, uint8_t n, uint8_t tile0, uint8_t tstep, uint8_t prop) NONBANKED {
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

/* Register-only common path, including clipping for one to four columns.
 * Larger unusual groups retain the portable path. Every visible column still
 * reserves the same bands and contiguous range before any sprite is written.
 * sdcccall(1): DE=x, BC=y; n/tile/step/prop are four callee-popped stack bytes. */
uint8_t oam_group(int16_t sx, int16_t sy, uint8_t n, uint8_t tile0, uint8_t tstep, uint8_t prop) NONBANKED NAKED {
  sx;sy;n;tile0;tstep;prop;
  __asm
    ld hl,#15
    add hl,bc
    ld a,h
    or a,a
    jp nz,8$
    ld a,l
    cp a,#159
    jp nc,8$
    ld a,c
    add a,#16
    ld (#_emit_y),a
    ldhl sp,#2
    ld a,(hl)
    or a,a
    jp z,8$
    cp a,#5
    jr nc,20$
    ld a,d
    or a,a
    jr z,21$
    cp a,#255
    jp nz,_oam_clipped
    ld a,e
    or a,a
    jp z,8$
    cpl
    inc a
    srl a
    srl a
    srl a
    ld c,a
    ld a,(hl)
    sub a,c
    jp z,8$
    jp c,8$
    ld (hl),a
    ld a,c
    add a,a
    add a,a
    add a,a
    add a,e
    ld e,a
    ldhl sp,#4
    ld d,(hl)
    dec hl
    ld a,(hl)
    ld b,c
    inc b
  22$:
    dec b
    jr z,23$
    add a,d
    jr 22$
  23$:
    ld (hl),a
    jr 24$
  21$:
    ld a,e
    cp a,#160
    jp nc,8$
    ld a,#167
    sub a,e
    srl a
    srl a
    srl a
    ld c,a
    ld a,(hl)
    cp a,c
    jr c,24$
    ld (hl),c
    jr 24$
  20$:
    ld a,d
    or a,a
    jp nz,_oam_clipped
    ld a,(hl)
    add a,a
    add a,a
    add a,a
    ld l,a
    ld a,#160
    sub a,l
    cp a,e
    jp c,_oam_clipped
  24$:
    ldhl sp,#2
    ld a,(hl)
    ld (#_emit_n),a
    ld a,e
    add a,#8
    ld (#_emit_x),a
    ld a,(#_emit_n)
    ld b,a
    ld a,(#_oam_n)
    add a,b
    ld c,a
    ld a,(#_oam_cap)
    cp a,c
    jr c,7$
    ld e,b
    ld a,(#_emit_y)
    call _reserve
    or a,a
    jr z,7$
    ld a,(#_oam_n)
    ld l,a
    add a,b
    ld (#_oam_n),a
    ld a,l
    add a,a
    add a,a
    add a,#<_shadow_OAM
    ld l,a
    ld h,#>_shadow_OAM
    push hl
    ldhl sp,#5
    ld a,(hl+)
    ld d,a
    ld a,(hl+)
    ld c,a
    ld a,(hl)
    ld e,a
    pop hl
  1$:
    ld a,(#_emit_y)
    ld (hl+),a
    ld a,(#_emit_x)
    ld (hl+),a
    add a,#8
    ld (#_emit_x),a
    ld a,d
    ld (hl+),a
    add a,c
    ld d,a
    ld a,e
    ld (hl+),a
    dec b
    jr nz,1$
  8$:
    ld a,#1
    jr 9$
  7$:
    ld a,b
    ld hl,#_oam_refused
    add a,(hl)
    ld (hl),a
    xor a,a
  9$:
    pop hl
    add sp,#4
    jp (hl)
  __endasm;
}

void oam_count(uint8_t from) NONBANKED NAKED {
  from;
  __asm
    ld c,a
    ld a,(#_oam_n)
    sub a,c
    ret z
    ld b,a
    ld a,c
    add a,a
    add a,a
    add a,#<_shadow_OAM
    ld e,a
    ld d,#>_shadow_OAM
  1$:
    ld a,(de)
    or a,a
    jr z,3$
    cp a,#160
    jr nc,3$
    ld c,a
    rrca
    rrca
    and a,#0x3f
    add a,#<_band
    ld l,a
    ld a,#>_band
    adc a,#0
    ld h,a
    inc (hl)
    inc hl
    inc (hl)
    inc hl
    inc (hl)
    inc hl
    inc (hl)
    ld a,c
    and a,#3
    jr z,3$
    inc hl
    inc (hl)
  3$:
    ld a,e
    add a,#4
    ld e,a
    dec b
    jr nz,1$
    ret
  __endasm;
}
static void clear_bands(void) NONBANKED NAKED {
  __asm
    ld hl,#_band
    xor a,a
    .rept 44
      ld (hl+),a
    .endm
    ret
  __endasm;
}
typedef char native_band_count[(OAM_BANDS+2u==44u) ? 1 : -1];

uint8_t oam_band_max(void) NONBANKED {
  uint8_t i, m = 0;
  for (i = 0; i != OAM_BANDS; i++)
    if (band[i] > m) m = band[i];
  return m;
}

/* The look moves during hitstop and pause. Recount without advancing poses. */
void sprites_budget(void) NONBANKED {
  uint8_t k, y;
  clear_bands();
  for (k = 0; k != 40u; k++) {
    y = shadow_OAM[k].y;
    if (!y || y >= 160u || !shadow_OAM[k].x || shadow_OAM[k].x >= 168u) continue;
    if (!reserve(y, 1u)) { shadow_OAM[k].y = 0; oam_refused++; }
  }
}

static void untake_card(uint8_t k) NONBANKED NAKED {
  k;
  __asm
    add a,a
    add a,a
    add a,#<_shadow_OAM
    ld l,a
    ld h,#>_shadow_OAM
    ld a,(hl+)
    or a,a
    ret z
    cp a,#160
    ret nc
    ld d,a
    ld a,(hl)
    or a,a
    ret z
    cp a,#168
    ret nc
    ld a,d
    rrca
    rrca
    and a,#0x3f
    add a,#<_band
    ld l,a
    ld a,#>_band
    adc a,#0
    ld h,a
    .rept 4
      ld a,(hl)
      or a,a
      jr z,.+3
      dec (hl)
      inc hl
    .endm
    ld a,d
    and a,#3
    ret z
    ld a,(hl)
    or a,a
    ret z
    dec (hl)
    ret
  __endasm;
}

void oam_card_pair(uint8_t a, uint8_t b, uint8_t x0, uint8_t x1, uint8_t y,
                   uint8_t tile, uint8_t prop) NONBANKED {
  uint8_t n = (uint8_t)((x0 && x0 < 168u) + (x1 && x1 < 168u));
  untake_card(a);
  if (b != a) untake_card(b);
  shadow_OAM[a].y = shadow_OAM[b].y = 0;
  if (!n || !y || y >= 160u) return;
  if (!reserve(y, n)) { oam_refused += n; return; }
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
/* No projection or allocation is needed after the FX deadline. Age every
 * existing puff in place, preserving its lifetime without a C pointer walk. */
static void age_puffs(void) NAKED {
  __asm
    ld hl,#_puffs
    ld de,#5
    ld b,#6
  1$:
    ld a,(hl)
    or a,a
    jr z,2$
    dec (hl)
  2$:
    add hl,de
    dec b
    jr nz,1$
    ret
  __endasm;
}
typedef char puff_native_stride[(sizeof(puff_t)==5u && MAXP==6u) ? 1 : -1];

/* Projectiles retain their full twelve-bit pixel coordinates. The stack packet
 * avoids spilling each source member and projection through the outer loop. */
static void draw_orbs(void) NAKED {
  __asm
    add sp,#-8
    ld de,#_orbs
    ldhl sp,#0
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (hl),#8
  1$:
    ldhl sp,#0
    ld a,(hl+)
    ld h,(hl)
    ld l,a
    ld a,(hl+)
    or a,a
    jp z,8$
    bit 7,(hl)
    jp nz,8$
    ld a,(hl)
    ld b,#4
    or a,a
    jr nz,2$
    inc b
  2$:
    inc hl
    inc hl
    inc hl
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    inc hl
    ld c,l
    ld a,h
    ldhl sp,#3
    ld (hl),b
    ld b,a
    .rept 4
      srl d
      rr e
    .endm
    ld a,e
    sub a,#4
    ld e,a
    ld a,d
    sbc a,#0
    ld d,a
    ld a,e
    ld hl,#_cam_x
    sub a,(hl)
    ld e,a
    ld a,d
    sbc a,#0
    ld d,a
    ldhl sp,#4
    ld (hl),e
    inc hl
    ld (hl),d
    ld h,b
    ld l,c
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    .rept 4
      srl d
      rr e
    .endm
    ld a,e
    sub a,#4
    ld e,a
    ld a,d
    sbc a,#0
    ld d,a
    ld a,e
    ld hl,#_cam_y
    sub a,(hl)
    ld e,a
    ld a,d
    sbc a,#0
    ld d,a
    ldhl sp,#6
    ld (hl),e
    inc hl
    ld (hl),d
    ldhl sp,#3
    ld a,(hl)
    push af
    inc sp
    xor a,a
    push af
    inc sp
    ld a,#132
    push af
    inc sp
    ld a,#1
    push af
    inc sp
    ldhl sp,#8
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    inc hl
    ld a,(hl+)
    ld c,a
    ld b,(hl)
    call _oam_group
  8$:
    ldhl sp,#0
    ld a,(hl+)
    ld h,(hl)
    ld l,a
    ld de,#10
    add hl,de
    ld e,l
    ld d,h
    ldhl sp,#0
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    dec (hl)
    jp nz,1$
    add sp,#8
    ret
  __endasm;
}
typedef char native_orb_draw_layout[(MAXO==8u && sizeof(orb_t)==10u && offsetof(orb_t,live)==0u &&
  offsetof(orb_t,friendly)==1u && offsetof(orb_t,x)==4u && offsetof(orb_t,y)==6u && ST_ORB==132u) ? 1 : -1];

#include "sprites_start.inc"
void draw_sprites(void) BANKED {
  uint8_t k, fr, start;
  int16_t x, y;
  oam_n = 0;
  oam_refused = 0;
  oam_cap = look_busy_n ? 24u : 40u;
  if (look_busy_n && !was_busy)
    hide_range(24u,40u);
  oam_spin++;
  clear_bands();
  /* OP_HEIR: the heir (heir.c), BODY layer over WEAR layer, its box 16x22 with the feet at pl_y + 6 */
  heir_tick();
  if ((pl_state!=PS_FALL || pl_t<24u) && (!pl_hp || !(pl_inv && (pl_inv & 4u)) || pl_state == PS_HURT || ha_state == HA_SPAWN)) {
    /* heir_draw writes hardware OAM coordinates; its X already includes the hardware's 8-pixel bias. */
    /* heir_draw accepts hardware Y (the LCD subtracts 16). With the WEAR
     * layer at +6, +1 puts the feet at centre+6, on the 12x12 footprint.
     * The old -7 lifted the entire character eight pixels off its collider. */
    oam_n = (uint8_t)(oam_n + heir_draw(oam_n, (int16_t)pl_x-cam_x, (int16_t)pl_y+1-cam_y-pl_z));
    oam_count(0);
  }
  /* OP_WEAPON: aim and arc share all eight directions. */
  if (pl_arc == 1u || (pl_arc==2u && heir.weapon==1u)) {
    uint8_t reach=heir.weapon==1u ? 12u : heir.weapon==2u ? 24u : heir.weapon==3u ? 10u : heir.weapon==4u ? 20u : 14u;
    if(mods&MOD_LONG) reach+=4u;
    fr=(uint8_t)((heir.weapon==1u ? pin.phase!=EG_PH_STARTUP : pl_t>=5u) ? 4u : 0u);
    if(heir.weapon==1u)reach=fr ? 16u : 8u;
    if(pmv.facing&1u) {
      uint8_t left=(uint8_t)(pmv.facing>=5u),down=(uint8_t)(pmv.facing==3u || pmv.facing==5u);
      reach=(uint8_t)(reach-(reach>>2));
      x=(int16_t)pl_x+(left ? -(int8_t)reach : (int8_t)reach);
      y=(int16_t)pl_y+(down ? (int8_t)reach : -(int8_t)reach);
      if(left) oam_group(x-cam_x-8,y-cam_y-8,2u,2u,254u,(uint8_t)(4u|S_BANK|S_FLIPX|(down ? S_FLIPY : 0u)));
      else oam_group(x-cam_x-8,y-cam_y-8,2u,0u,2u,(uint8_t)(4u|S_BANK|(down ? S_FLIPY : 0u)));
    } else {
      k=pmv.facing==0u ? F_UP : pmv.facing==2u ? F_RIGHT : pmv.facing==4u ? F_DOWN : F_LEFT;
      x=(int16_t)pl_x+fdx[k]*(int8_t)reach;y=(int16_t)pl_y+fdy[k]*(int8_t)reach;
      if(k==F_UP) spr16(x,y,(uint8_t)(ST_ARC_UP+fr),4u,0u);
      else if(k==F_DOWN) spr16(x,y,(uint8_t)(ST_ARC_DOWN+fr),4u,0u);
      else spr16(x,y,(uint8_t)(ST_ARC_SIDE+fr),4u,(uint8_t)(k==F_LEFT));
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
  /* OP_SHOT: projectiles keep their priority ahead of mob bodies. */
  if(orbs[0].live|orbs[1].live|orbs[2].live|orbs[3].live|orbs[4].live|orbs[5].live|orbs[6].live|orbs[7].live)draw_orbs();
  if(elemental_active)elements_draw();
  if(companion.id)companion_draw();
  /* OP_MOB: bodies, from a rotating start so a crowded line flickers fairly */
  if (room.nob) obstacle_draw();
  if (nen && !(boss_floor && nen == 1u)) {
    start = sprite_start(oam_spin,nen);
    k = start;
    do {
      enemy_t *e = &en[k];
      if (e->st != ES_DEAD) {
        if (e->kind == SPAWN_MOB) mob_draw(k);
      }
      if (++k == nen) k = 0;
    } while (k != start);
    /* OP_OVER */
    mobs_draw(OP_OVER);
  }
  /* OP_FX: particles, then puffs (cosmetic: left out where a line is full) */
  health_draw();
  fx_draw();
  if(puffs[0].t|puffs[1].t|puffs[2].t|puffs[3].t|puffs[4].t|puffs[5].t) {
    puff_t *p = puffs;
    int16_t py;
    uint8_t quota=(uint8_t)(LY_REG>=88u ? 0u : MAXP);
    if(!quota)age_puffs();
    else for (k = MAXP; k; k--, p++)
      if (p->t) {
        p->t--;
        if(quota && video_epoch==frame_epoch && !(LY_REG>=88u && LY_REG<144u)) {
          py = (int16_t)p->y - (int16_t)((14u - p->t) >> 2);
          spr8((int16_t)p->x, py, p->tile, p->pal);
          quota--;
        }
      }
  }
  if (look_busy_n && was_busy) {
    /* Flying cards occupy the high OAM slots outside the actor allocator. They
     * still share the hardware's ten-OBJ line limit with actors and shots. */
    if(game_mode==MODE_PLAY && !bench_on && !boss_floor &&
       stage_type!=SG_DARK && stage_type!=SG_CHASE) {
      /* The late look pass rebuilds these after actors, against their fresh
       * bands. Do not reserve yesterday's cards only to remove them again. */
      hide_range(24u,40u);
    } else for (k = 24u; k != 40u; k++) {
        uint8_t y = shadow_OAM[k].y;
        if (!y) continue;
        if (!reserve(y, 1u)) { shadow_OAM[k].y = 0; oam_refused++; }
      }
  }
  if(!look_busy_n && was_busy) hide_range(oam_n,40u);
  else hide_range(oam_n,oam_last<oam_cap ? oam_last : oam_cap);
  was_busy = look_busy_n != 0u;
  oam_last = oam_n;
  { extern uint8_t bo_refused; bo_refused = oam_refused; }
}
