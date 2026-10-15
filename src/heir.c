/* The heir: rolled from a seed exactly as proto/genetics/genes.py founder() rolls it, its palettes, the animation
 * player over the baked state table (src/gen/heir_tab.c) and the idle scheduler (src/core/eg_idle.c). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "heir.h"
#include "core/eg_input.h"

heir_t heir;
uint16_t dbg_heir;
uint8_t dbg_heir_state, dbg_heir_dir, opt_move4;
uint8_t ha_state = 0xffu, ha_frame, ha_dir, ha_done;
static uint8_t ha_left, ha_n, ha_loop, ha_wrap;
static heir_frame_t ha_fr;
static eg_idle_t idl;
static uint8_t idle_on, idle_n;
static uint8_t shown_pose = 0xffu, shown_dd, buf_sel, spr_mask;

/* --- the roll: genes.founder(Rng(seed * 7919)) and genes.express(), the loci the floor uses --- */
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
  rs = (uint32_t)seed * 7919ul;
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
    eg_idle_init(&idl, (uint16_t)(heir.seed + (uint16_t)idle_n * 0x9e37u), tr, ctx);
    idle_n++;
    idle_on = 1;
    heir_play(idle_state(idl.seg), 1);
    return;
  }
  if (eg_idle_step(&idl)) {
    heir_play(idle_state(idl.seg), 1);
  } else if (idl.seg == EG_ID_BASE && ha_wrap) {
    /* the base loop: a breath, then a blink, and again */
    heir_play(ha_state == HA_IDLE ? HA_IDLE_BLINK : HA_IDLE, 1);
  }
}

void heir_idle_stop(void) BANKED { idle_on = 0; }

void heir_reset(void) BANKED { shown_pose = 0xffu; }

/* the four sprites: BODY at box row 1, WEAR at row 6, two columns; X-flip mirrors E/SE/NE into W/SW/NW */
uint8_t heir_draw(uint8_t oam, int16_t sx, int16_t sy) BANKED {
  uint8_t fin, dd, flip, s, n = 0, pal, tile;
  int16_t x, y;
  if (ha_state == 0xffu) return 0;
  fin = ha_fr.dir == HEIR_DIR_CAM ? 0u : (uint8_t)((ha_dir + ha_fr.dir) & 7u);
  flip = (uint8_t)(fin >= 5u);
  dd = flip ? (uint8_t)(8u - fin) : fin;
  if (ha_fr.pose != shown_pose || dd != shown_dd) {
    buf_sel ^= 1u;
    spr_mask = heir_compose(ha_fr.pose, dd, (uint8_t)(buf_sel << 3));
    shown_pose = ha_fr.pose;
    shown_dd = dd;
  }
  for (s = 0; s != 4u; s++) {
    if (!(spr_mask & (1u << s)) || oam + n > 39u) continue;
    x = sx + (int16_t)((((s & 1u) ^ flip)) << 3);
    y = sy + (s < 2u ? 1 : 6);
    if (x < -8 || x > 168 || y < 0 || y > 160) continue;
    tile = (uint8_t)((buf_sel << 3) + (s << 1));
    pal = s < 2u ? 0u : 1u;
    if (ha_fr.flash == HEIR_FLASH_WHITE) pal = 6u;
    else if (ha_fr.flash == HEIR_FLASH_RED) pal = 7u;
    set_sprite_tile((uint8_t)(oam + n), tile);
    set_sprite_prop((uint8_t)(oam + n), (uint8_t)(pal | (flip ? S_FLIPX : 0u)));
    move_sprite((uint8_t)(oam + n), (uint8_t)x, (uint8_t)y);
    n++;
  }
  return n;
}
