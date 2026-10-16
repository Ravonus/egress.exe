/* The heir: rolled from a seed exactly as proto/genetics/genes.py founder() rolls it, its palettes, the animation
 * player over the baked state table (src/gen/heir_tab.c) and the idle scheduler (src/core/eg_idle.c). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "heir.h"
#include "narrator.h"
#include "core/eg_input.h"

heir_t heir;
uint16_t dbg_heir;
uint8_t dbg_heir_state, dbg_heir_dir, opt_move4;
uint8_t ha_state = 0xffu, ha_frame, ha_dir, ha_done;
static uint8_t ha_left, ha_n, ha_loop, ha_wrap;
static heir_frame_t ha_fr;
eg_idle_t heir_idl; /* not static: the harness reads heir_idl.t (ticks since the idle began) */
static uint8_t idle_on, idle_n;
static uint8_t shown_pose = 0xffu, shown_dd, buf_sel, spr_mask;
/* the heir's OAM entries as offsets: (dy, dx, tile, attributes) per drawn sprite, OAM order */
static uint8_t d_n, d_ent[16], d_flip = 0xffu, d_flash = 0xffu, d_sel = 0xffu;
static uint8_t hd_x, hd_y, hd_n;
static uint8_t *hd_dst;

/* hd_n entries of d_ent to hd_dst with hd_x/hd_y added: a moving frame's whole OAM cost */
static void oam_write(void) NAKED {
  __asm
    ld hl, #_hd_n
    ld a, (hl)
    or a, a
    ret z
    ld c, a
    ld hl, #_hd_dst
    ld a, (hl+)
    ld d, (hl)
    ld e, a
    ld hl, #_d_ent
  1$:
    ld a, (#_hd_y)
    add a, (hl)
    inc hl
    ld (de), a
    inc de
    ld a, (#_hd_x)
    add a, (hl)
    inc hl
    ld (de), a
    inc de
    ld a, (hl+)
    ld (de), a
    inc de
    ld a, (hl+)
    ld (de), a
    inc de
    dec c
    jr nz, 1$
    ret
  __endasm;
}

/* --- the roll: genes.founder(Rng(seed * 0x9E3779B1 ^ 0x5BD1E995)) and genes.express(), the loci the floor uses --- */
static uint32_t rs;
static uint8_t u8(void) {
  rs = rs * 1103515245ul + 12345ul;
  return (uint8_t)(rs >> 16);
}
static uint8_t pick(uint8_t n) { return (uint8_t)(((uint16_t)u8() * n) >> 8); }

static const uint8_t cloth_for_tag[7] = {5, 1, 2, 6, 3, 4, 7}, cloth_alt_for_tag[7] = {6, 2, 1, 7, 2, 7, 4};
/* genes.TRAITS order: HEAVY SMALL ECHO GLITCHED INSOMNIAC REMEMBERS DAMP LOUD COLOURBLIND ALLERGIC */
static const uint8_t trait_bit[10] = {HT_HEAVY, HT_SMALL, HT_OTHER, HT_GLITCHED, HT_INSOMNIAC,
                                      HT_OTHER, HT_OTHER, HT_OTHER, HT_OTHER, HT_OTHER};
#define RECESSIVE_MASK 0x012cu /* ECHO 2, GLITCHED 3, REMEMBERS 5, COLOURBLIND 8 */

void heir_roll(uint16_t seed) BANKED {
  uint8_t d0, d1, w0, k, a, b, n = 0, hair, acc, aff[7], top = 0;
  rs = ((uint32_t)seed * 0x9E3779B1ul) ^ 0x5BD1E995ul; /* spread: neighbouring seeds give unrelated heirs */
  if (!rs) rs = 1ul;
  heir.skin = pick(6);
  pick(8); /* face shape */
  pick(2); /* mouth */
  hair = pick(5);
  heir.neck = pick(4);
  heir.pat = pick(8);
  pick(6); /* face dither */
  pick(3); /* size (drawn at its middle value) */
  acc = pick(8);
  heir.mark = pick(8);
  heir.look = pick(3);
  d0 = pick(3);
  d1 = pick(3);
  w0 = pick(5);
  pick(5);
  heir.traits = 0;
  for (k = 0; k != 10u; k++) {
    a = (uint8_t)(u8() < 40u);
    b = (uint8_t)(u8() < 40u);
    if (((RECESSIVE_MASK >> k) & 1u) ? (a && b) : (a || b)) {
      if (n < 2u) heir.traits |= trait_bit[k];
      n++;
    }
  }
  for (k = 0; k != 7u; k++) {
    aff[k] = u8() < 90u ? pick(3) : 0u;
    if (aff[k] > aff[top]) top = k;
  }
  /* express */
  heir.dodge = (d0 == 0u || d1 == 0u) ? EG_DODGE_ROLL : ((d0 == 1u || d1 == 1u) ? EG_DODGE_SLIDE : EG_DODGE_BLINK);
  heir.weapon = w0;
  heir.accent = aff[top] >= 2u ? ((acc & 1u) ? cloth_alt_for_tag[top] : cloth_for_tag[top]) : acc;
  if (heir.look == 1u) heir.hair = (hair == 1u || hair == 4u) ? 4u : 1u;
  else if (heir.look == 2u) heir.hair = (hair & 1u) ? 3u : 2u;
  else heir.hair = hair;
  heir.seed = (uint16_t)(seed * 40503u + 1u);
  heir_compose_init();
  shown_pose = 0xffu;
  d_sel = 0xffu;
}

void heir_colours(void) BANKED {
  uint16_t p[8];
  uint8_t k;
  heir_get_pal(heir.skin, heir.accent, p);
  for (k = 1; k != 4u; k++) pal_sp[k] = p[k];
  pal_sp[5] = p[5];
  pal_sp[6] = p[6];
  pal_sp[7] = RGB8(80, 232, 248); /* the rim: NOCLIP's cyan (the biome accent) */
}

/* --- the animation player --- */
static void load_frame(void) {
  heir_get_frame(ha_state, ha_frame, &ha_fr);
  ha_left = ha_fr.dur;
}

void heir_play(uint8_t state, uint8_t restart) BANKED {
  if (state == ha_state && !restart) return;
  if(state==HA_LONG_DOZE && ha_state!=state) narrator_event(NE_DOZE,0u);
  ha_state = state;
  ha_frame = 0;
  ha_done = 0;
  ha_wrap = 0;
  ha_n = heir_state_len(state, &ha_loop);
  load_frame();
}

void heir_tick(void) BANKED {
  if (ha_state == 0xffu) return;
  if (ha_left > 1u) {
    ha_left--;
    return;
  }
  if ((uint8_t)(ha_frame + 1u) < ha_n) {
    ha_frame++;
    load_frame();
  } else if (ha_loop) {
    ha_frame = 0;
    ha_wrap = 1;
    load_frame();
  } else
    ha_done = 1;
}

/* the scheduler's segments as states (lying down needs a 3-wide sprite layout: it dozes sitting instead) */
static uint8_t idle_state(uint8_t seg) {
  if (seg >= EG_ID_F_LOOK && seg <= EG_ID_F_BOUNCE) return (uint8_t)(HA_FIDGET_LOOK + (seg - EG_ID_F_LOOK));
  switch (seg) {
    case EG_ID_LONG_SIT: return HA_LONG_SIT;
    case EG_ID_LONG_SIT_TWITCH: return HA_LONG_SIT_TWITCH;
    case EG_ID_LONG_DOZE:
    case EG_ID_LONG_LIE: return HA_LONG_DOZE;
    case EG_ID_READY: return HA_CTX_READY;
    case EG_ID_COOL: return HA_CTX_COOL;
    case EG_ID_LOWHP: return HA_CTX_LOWHP;
    default: return HA_IDLE;
  }
}

void heir_idle(uint8_t ctx) BANKED {
  uint8_t tr = 0;
  if (!idle_on) {
    if (heir.traits & HT_INSOMNIAC) tr |= EG_IT_INSOMNIAC;
    if (heir.traits & HT_HEAVY) tr |= EG_IT_HEAVY;
    if (heir.traits & HT_GLITCHED) tr |= EG_IT_GLITCHED;
    if (heir.traits & HT_SMALL) tr |= EG_IT_SMALL;
    eg_idle_init(&heir_idl, (uint16_t)(heir.seed + (uint16_t)idle_n * 0x9e37u), tr, ctx);
    idle_n++;
    idle_on = 1;
    heir_play(idle_state(heir_idl.seg), 1);
    return;
  }
  if (eg_idle_step(&heir_idl)) {
    heir_play(idle_state(heir_idl.seg), 1);
  } else if (heir_idl.seg == EG_ID_BASE && ha_wrap) {
    /* the base loop: a breath, then a blink, and again */
    heir_play(ha_state == HA_IDLE ? HA_IDLE_BLINK : HA_IDLE, 1);
  }
}

void heir_idle_stop(void) BANKED { idle_on = 0; }

void heir_reset(void) BANKED {
  shown_pose = 0xffu;
  d_sel = 0xffu;
}

/* the four sprites: BODY at box row 1, WEAR at row 6, two columns; X-flip mirrors E/SE/NE into W/SW/NW. The
 * sprite list (offsets, tiles, attributes) is rebuilt only when the pose, flip or flash changes: a frame that only
 * moves writes four OAM entries with 8-bit adds. */
uint8_t heir_draw(uint8_t oam, int16_t sx, int16_t sy) BANKED {
  uint8_t fin, dd, flip, s, n, pal, *e;
  OAM_item_t *o;
  if (ha_state == 0xffu) return 0;
  fin = ha_fr.dir == HEIR_DIR_CAM ? 0u : (uint8_t)((ha_dir + ha_fr.dir) & 7u);
  flip = (uint8_t)(fin >= 5u);
  dd = flip ? (uint8_t)(8u - fin) : fin;
  /* Reuse the current full body for one tick when gravity or a late boss effect
   * leaves too little rendering time for composition. Never hide the body. */
  if ((ha_fr.pose != shown_pose || dd != shown_dd) &&
      (shown_pose==0xffu || (grav_t!=GRAVITY_FRAMES && (LY_REG<64u || LY_REG>=144u)))) {
    buf_sel ^= 1u;
    spr_mask = heir_compose(ha_fr.pose, dd, (uint8_t)(buf_sel << 3));
    shown_pose = ha_fr.pose;
    shown_dd = dd;
    d_sel = 0xffu;
  }
  if (flip != d_flip || ha_fr.flash != d_flash || buf_sel != d_sel) {
    d_flip = flip;
    d_flash = ha_fr.flash;
    d_sel = buf_sel;
    pal = d_flash == HEIR_FLASH_WHITE ? 6u : (d_flash == HEIR_FLASH_RED ? 7u : 0xffu);
    e = d_ent;
    d_n = 0;
    for (s = 0; s != 4u; s++) {
      if (!(spr_mask & (1u << s))) continue;
      *e++ = s < 2u ? 1u : 6u;
      *e++ = (uint8_t)(((s & 1u) ^ flip) << 3);
      *e++ = (uint8_t)((buf_sel << 3) + (s << 1));
      *e++ = (uint8_t)((pal != 0xffu ? pal : (s < 2u ? 0u : 1u)) | (flip ? S_FLIPX : 0u));
      d_n++;
    }
  }
  n = d_n;
  if ((uint8_t)(oam + n) > 40u) n = oam < 40u ? (uint8_t)(40u - oam) : 0u;
  if (!((uint16_t)sx & 0xff00u) && (uint8_t)sx <= 152u && !((uint16_t)sy & 0xff00u) && (uint8_t)sy <= 154u) {
    hd_x = (uint8_t)sx; /* the usual case: on screen, 8-bit adds in oam_write */
    hd_y = (uint8_t)sy;
    hd_n = n;
    hd_dst = (uint8_t *)&shadow_OAM[oam];
    oam_write();
    return n;
  }
  o = &shadow_OAM[oam];
  e = d_ent;
  fin = 0;
  for (s = n; s; s--, e += 4) {
    int16_t xx = sx + e[1], yy = sy + e[0];
    if (xx < -8 || xx > 168 || yy < 0 || yy > 160) continue;
    o->y = (uint8_t)yy;
    o->x = (uint8_t)xx;
    o->tile = e[2];
    o->prop = e[3];
    o++;
    fin++;
  }
  return fin;
}
