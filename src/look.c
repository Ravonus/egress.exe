/* The look: the floor's cards, the biome palettes and their ambient life, and the world falling into place and
 * falling apart (docs/redesign.md sections 1 and 2; the layout and the budgets are in src/look.h). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "look.h"
#include "oam.h"
#include "stage.h"
#include "core/eg_view.h"
#include "world.h"
#include "world_play.h"
#include "heir.h"
#include "core/eg_input.h"
extern eg_input_t pin;
void items_tick(void) BANKED;
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;

uint8_t look_present[EG_N / 8u];
uint8_t look_busy_n;
/* Floor-only skins alias the heir picker's DD00..DE7F scratch. Neither is live
 * during the other phase; look_load clears this before every floor. The world
 * retains 560 bytes above DDCF for the stack, rather than consuming hot BSS. */
uint8_t __at(0xDCF0) look_skin[EG_N];
uint8_t look_glitch_phase, look_glitch_period = 48u;
uint8_t look_dbg_cell, look_dbg_kind;

static uint8_t queued[EG_N / 8u],in_flight[EG_N/8u];
static uint8_t q[256], q_head, q_n; /* the wave: cells in order, a ring */
uint8_t roam_hidden[EG_N/8u];
static uint8_t roam_on,roam_scan,roam_left,roam_x,roam_y,near_cursor,roam_dir;
static uint8_t roam_late,prep_cursor;

/* cards in flight (assembly) and pieces falling (collapse) */
enum { K_FREE = 0, K_DROP = 1, K_FALL = 7, K_WOBBLE = 8, K_SHATTER = 9 };
static uint8_t c_kind[LOOK_MAX_CARDS], c_cell[LOOK_MAX_CARDS], c_t[LOOK_MAX_CARDS], c_meta[LOOK_MAX_CARDS];
static uint8_t c_brk[LOOK_MAX_CARDS]; /* a break's fragile kind (LOOK_FRAG_*), 0 for the wave's cards */
static uint8_t n_break;
static uint16_t brk_obj[8]; /* OBJ 6 and 7 while a break is in the air: the card's palette, and darker */
static uint8_t redraw[EG_N / 8u], rd_any,rd_cursor,rd_left;
static uint8_t gl_any, gl_t, gl_step;
static int16_t c_top[LOOK_MAX_CARDS], c_bot[LOOK_MAX_CARDS]; /* the vertical span its path covers (world px) */
static uint8_t n_flying;

static uint8_t on, biome_now, obj_mode, obj_want, budget;
static uint16_t saved_bg[32];
static uint16_t obj_pal[4][4]; /* 0 floor card, 1 wall card, 2 falling, 3 falling deeper */
static uint16_t amber[4];
static uint16_t dim0[4];
static uint16_t hum[2]; /* the open door's pulse: the HUD/door palette's accent, and brighter */
static uint16_t lk_rs;
static uint8_t flick_next, flick_t;

/* A fixed downward arrival: drop, decelerate, one-pixel settle, commit. */
static const int8_t ease_in[5] = {-16, -9, -3, 1, 0};
#define EASE_FRAMES 5u
/* the fall: crack, split, accelerate, drift apart */
static const uint8_t fall_dy[12] = {0, 1, 2, 4, 6, 9, 12, 16, 21, 26, 32, 38};
#define FALL_FRAMES 12u

static uint8_t lk_rnd(void) {
  lk_rs = (uint16_t)(lk_rs * 25173u + 13849u);
  return (uint8_t)(lk_rs >> 8);
}

static uint16_t mix(uint16_t a, uint16_t b, uint8_t w16) {
  int8_t r = (int8_t)(a & 31u), g = (int8_t)((a >> 5) & 31u), bl = (int8_t)((a >> 10) & 31u);
  int8_t r2 = (int8_t)(b & 31u), g2 = (int8_t)((b >> 5) & 31u), b2 = (int8_t)((b >> 10) & 31u);
  r = (int8_t)(r + (int8_t)(((int16_t)(r2 - r) * w16) >> 4));
  g = (int8_t)(g + (int8_t)(((int16_t)(g2 - g) * w16) >> 4));
  bl = (int8_t)(bl + (int8_t)(((int16_t)(b2 - bl) * w16) >> 4));
  return (uint16_t)((uint8_t)r | ((uint16_t)(uint8_t)g << 5) | ((uint16_t)(uint8_t)bl << 10));
}

void look_load(uint8_t biome) BANKED {
  uint8_t b, k;
  if (!on) memcpy(saved_bg, pal_bg, sizeof saved_bg);
  on = 1u;
  biome_now = biome;
  look_art_load(biome, pal_bg, meta_pal, &meta_tile[0][0]);
  for (k = 0; k != 4u; k++) {
    obj_pal[0][k] = pal_bg[LOOK_PAL_FLOOR * 4u + k];
    obj_pal[1][k] = pal_bg[LOOK_PAL_WALL * 4u + k];
    obj_pal[2][k] = mix(pal_bg[LOOK_PAL_FLOOR * 4u + k], pal_bg[LOOK_PAL_VOID * 4u + 1u], 6u);
    obj_pal[3][k] = mix(pal_bg[LOOK_PAL_FLOOR * 4u + k], pal_bg[LOOK_PAL_VOID * 4u + 1u], 11u);
    dim0[k] = mix(pal_bg[LOOK_PAL_FLOOR * 4u + k], pal_bg[LOOK_PAL_VOID * 4u], 3u);
  }
  b = (uint8_t)(LOOK_PAL_OBJ * 4u + 2u);
  amber[0] = amber[2] = pal_bg[b];
  amber[1] = mix(pal_bg[b], RGB(31, 31, 28), 5u);
  amber[3] = mix(pal_bg[b], pal_bg[LOOK_PAL_OBJ * 4u], 3u);
  hum[0] = pal_bg[LOOK_PAL_HUD * 4u + 2u];
  hum[1] = mix(hum[0], RGB(31, 31, 31), 9u);
  memset(look_present, 0xff, sizeof look_present);
  memset(queued, 0, sizeof queued);
  memset(in_flight,0,sizeof in_flight);
  memset(c_kind, 0, sizeof c_kind);
  memset(c_meta,255,sizeof c_meta);
  memset(c_brk, 0, sizeof c_brk);
  memset(look_skin, 0, sizeof look_skin);
  memset(redraw, 0, sizeof redraw);
  rd_any = rd_cursor = rd_left = gl_any = gl_t = gl_step = 0;
  look_glitch_phase = 0;
  n_break = 0;
  q_n = 0;
  n_flying = 0;
  look_busy_n = 0;
  obj_mode = obj_want = 0;
  roam_on=0u;prep_cursor=0u;
  lk_rs = (uint16_t)(floor_seed ^ 0x5a17u ^ ((uint16_t)floor_no << 4));
  flick_next = 120u;
  flick_t = 0;
}

static void hide_card(uint8_t k) {
  move_sprite((uint8_t)(LOOK_OAM_TOP - (k << 1)), 0, 0);
  move_sprite((uint8_t)(LOOK_OAM_TOP - 1u - (k << 1)), 0, 0);
}

void look_freeze(void) BANKED {
  uint8_t k;
  for (k = 0; k != LOOK_MAX_CARDS; k++) {
    c_kind[k] = K_FREE;
    c_brk[k] = 0;
    hide_card(k);
  }
  n_break = 0;
  q_n = 0;
  n_flying = 0;
  look_busy_n = 0;
  roam_on=0u;
  memset(in_flight,0,sizeof in_flight);
}

void look_end(void) BANKED {
  look_freeze();
  memset(look_present, 0xff, sizeof look_present);
  memset(queued, 0, sizeof queued);
  if (on) memcpy(pal_bg, saved_bg, sizeof saved_bg);
  on = 0;
  obj_mode = obj_want = 0; /* OBJ 6/7: the next pal_commit writes pal_sp's back */
}

/* --- the wave queue --- */
#define BIT(i) look_bits[(uint8_t)(i)&7u]
static void push(uint8_t i) {
  q[(uint8_t)(q_head + q_n)] = i;
  q_n++;
  queued[i >> 3] |= BIT(i);
}

static uint8_t on_screen(uint8_t i) {
  uint16_t x = (uint16_t)EG_X(i) << 4, y = (uint16_t)EG_Y(i) << 4;
  return (uint8_t)(x + 16u > cam_x && x < (uint16_t)cam_x + 160u && y + 16u > cam_y && y < (uint16_t)cam_y + 128u);
}

/* distance from the origin (an octagon), jittered by the floor: 0..63 */
static uint8_t wave_key(uint8_t i, uint8_t ox, uint8_t oy) {
  uint8_t x = EG_X(i), y = EG_Y(i), dx, dy;
  dx = x > ox ? (uint8_t)(x - ox) : (uint8_t)(ox - x);
  dy = y > oy ? (uint8_t)(y - oy) : (uint8_t)(oy - y);
  if (dx < dy) {
    x = dx;
    dx = dy;
    dy = x;
  }
  return (uint8_t)(dx * 3u + dy + ((uint8_t)(i * 37u + (uint8_t)floor_seed) >> 6));
}

/* mode 0: assembly (cells on screen, nearest first); 1: collapse (cells in the world, farthest first; those on
 * screen go first, so what you see starts falling at once) */
static uint8_t pass_vis;
static uint8_t eligible(uint8_t i, uint8_t mode, uint8_t origin, uint8_t keep) {
  uint8_t dx, dy;
  if (look_void_cell(i) || (queued[i >> 3] & BIT(i))) return 0;
  if (!mode) return (uint8_t)(i != origin && on_screen(i));
  if (!look_here(i) || on_screen(i) != pass_vis) return 0;
  if (keep) {
    dx = EG_X(i) > EG_X(origin) ? (uint8_t)(EG_X(i) - EG_X(origin)) : (uint8_t)(EG_X(origin) - EG_X(i));
    dy = EG_Y(i) > EG_Y(origin) ? (uint8_t)(EG_Y(i) - EG_Y(origin)) : (uint8_t)(EG_Y(origin) - EG_Y(i));
    if (dx < keep && dy < keep) return 0;
  }
  return 1u;
}

static void wave(uint8_t mode, uint8_t origin, uint8_t keep) {
  static uint8_t start[64];
  uint8_t i, k, ox = EG_X(origin), oy = EG_Y(origin), base = (uint8_t)(q_head + q_n), n = 0;
  memset(start, 0, sizeof start);
  i = 0;
  do {
    if (eligible(i, mode, origin, keep)) {
      k = wave_key(i, ox, oy);
      start[mode ? (uint8_t)(63u - k) : k]++;
      n++;
    }
  } while (++i != (uint8_t)EG_N);
  /* counts to starting offsets */
  for (i = 0, k = 0; i != 64u; i++) {
    uint8_t c = start[i];
    start[i] = k;
    k = (uint8_t)(k + c);
  }
  i = 0;
  do {
    if (eligible(i, mode, origin, keep)) {
      k = wave_key(i, ox, oy);
      if (mode) k = (uint8_t)(63u - k);
      q[(uint8_t)(base + start[k]++)] = i;
    }
  } while (++i != (uint8_t)EG_N);
  /* mark them queued (and, for assembly, out of the world) */
  for (k = 0; k != n; k++) {
    i = q[(uint8_t)(base + k)];
    queued[i >> 3] |= BIT(i);
    if (!mode) look_present[i >> 3] &= (uint8_t)~BIT(i);
  }
  q_n = (uint8_t)(q_n + n);
  look_busy_n = (uint8_t)(q_n + n_flying);
  obj_want = mode ? 2u : 1u;
}

void look_assemble(uint8_t origin) BANKED { wave(0, origin, 0); }
void look_collapse(uint8_t origin, uint8_t keep) BANKED {
  roam_on=0u;
  pass_vis = 1u;
  wave(1u, origin, keep);
  pass_vis = 0;
  wave(1u, origin, keep);
}

void look_drop_cell(uint8_t i) BANKED {
  if (!look_here(i) || look_void_cell(i) || (queued[i >> 3] & BIT(i))) return;
  push(i);
  look_busy_n = (uint8_t)(q_n + n_flying);
  obj_want = 2u;
}
void look_raise_cell(uint8_t i) BANKED {
  if (look_here(i) || (queued[i >> 3] & BIT(i))) return;
  push(i);
  look_busy_n = (uint8_t)(q_n + n_flying);
  obj_want = 1u;
}

void look_roam_begin(uint8_t enabled) BANKED {
  uint8_t i;
  roam_on=enabled;roam_scan=near_cursor=roam_dir=0u;roam_left=EG_N;roam_x=cam_x;roam_y=cam_y;
  memset(roam_hidden,0,sizeof roam_hidden);
  if(!enabled) return;
  for(i=0u;i!=EG_N;i++) if(!on_screen(i) && !look_void_cell(i) && cell_type(i)!=T_PIT) {
    roam_hidden[i>>3]|=BIT(i);look_present[i>>3]&=(uint8_t)~BIT(i);
  }
}

static uint8_t roam_pending(uint8_t i) {
  return (uint8_t)((queued[i>>3]|in_flight[i>>3])&BIT(i));
}
static uint8_t roam_wants(uint8_t i) {
  int16_t x=(int16_t)(EG_X(i)<<4)-(int16_t)cam_x;
  int16_t y=(int16_t)(EG_Y(i)<<4)-(int16_t)cam_y;
  /* Incoming slabs start just outside view. The trailing slab starts falling
   * while its last sixteen pixels are still visible. */
  if((roam_dir&J_RIGHT) && x<=8)return 0u;
  if((roam_dir&J_LEFT) && x>=136)return 0u;
  if((roam_dir&J_DOWN) && y<=8)return 0u;
  if((roam_dir&J_UP) && y>=104)return 0u;
  return (uint8_t)(x>-24 && x<168 && y>-24 && y<136);
}
static void roam_raise(uint8_t i) {
  if(!(roam_hidden[i>>3]&BIT(i)) || look_here(i) || roam_pending(i)) return;
  /* Keep active travel bounded by the card pool. Hidden cells remain marked
   * and are revisited; urgent floor under the feet uses look_anchor directly. */
  if((uint8_t)(q_n+n_flying)>=12u)return;
  /* Approaching floor precedes the cosmetic trailing queue. */
  q_head--;q[q_head]=i;q_n++;queued[i>>3]|=BIT(i);
  look_busy_n=(uint8_t)(q_n+n_flying);obj_want=1u;
}
static void roam_probe(uint8_t i) {
  if(roam_pending(i))return;
  if(roam_hidden[i>>3]&BIT(i)) {
    if(look_here(i)) {
      if(!look_void_cell(i) && cell_type(i)!=T_PIT)roam_hidden[i>>3]&=(uint8_t)~BIT(i);
    } else if(roam_wants(i) && !look_void_cell(i) && cell_type(i)!=T_PIT)roam_raise(i);
  } else if(look_here(i) && !roam_wants(i) && (uint8_t)(q_n+n_flying)<12u) {
    if(look_void_cell(i) || cell_type(i)==T_PIT)return;
    roam_hidden[i>>3]|=BIT(i);look_drop_cell(i);
  }
}
void look_roam_tick(void) BANKED {
  uint8_t k,i,probes,c,vertical,limit,a,b,cross;int8_t dx,dy;
  if(!roam_on || pl_state==PS_FALL || pl_state==PS_EXIT) return;
  dx=cam_x>roam_x ? 1 : cam_x<roam_x ? -1 : 0;
  dy=cam_y>roam_y ? 1 : cam_y<roam_y ? -1 : 0;
  if(dx || dy) {
    roam_left=EG_N;
    roam_dir=(uint8_t)((dx>0 ? J_RIGHT : dx<0 ? J_LEFT : 0u)|(dy>0 ? J_DOWN : dy<0 ? J_UP : 0u));
  }
  roam_x=cam_x;roam_y=cam_y;

  if((world_on || enemies_alive) && (video_epoch!=frame_epoch || (LY_REG>=80u && LY_REG<144u)))return;
  /* Sweep both visible edges instead of letting a whole-room scan discover
   * the trailing slab only after it has disappeared. Keep the old six-probe
   * ceiling; a diagonal alternates its horizontal and vertical edges. */
  vertical=(uint8_t)((roam_dir&(J_UP|J_DOWN)) && (!(roam_dir&(J_LEFT|J_RIGHT)) || (frame_count&1u)));
  limit=vertical ? 11u : 9u;
  if(prep_cursor>=limit)prep_cursor=0u;
  a=vertical ? cam_y : cam_x;
  /* Discover an approaching card half a slab before it enters the screen,
   * rather than discovering it and safety-landing it on the same edge row. */
  if(roam_dir&(vertical ? J_UP : J_LEFT))a=a>=8u ? (uint8_t)(a-8u) : 0u;
  a>>=4;
  b=(uint8_t)(((uint16_t)(vertical ? cam_y : cam_x)+
    (vertical ? ((roam_dir&J_DOWN) ? 135u : 127u) : ((roam_dir&J_RIGHT) ? 167u : 159u)))>>4);
  cross=(vertical ? cam_x : cam_y)>>4;
  for(k=0u;roam_dir && k<(roam_late ? 4u : 2u) && (uint8_t)(q_n+n_flying)<12u;k++) {
    if(video_epoch!=frame_epoch || (LY_REG>=(enemies_alive ? 48u : 64u) && LY_REG<144u))break;
    c=(uint8_t)(cross+prep_cursor);
    i=(k&1u) ? a : b;
    if(vertical) {if(c<EG_W && i<EG_H)roam_probe(EG_IDX(c,i));}
    else if(c<EG_H && i<EG_W)roam_probe(EG_IDX(i,c));
    if(k&1u) {if(++prep_cursor==limit)prep_cursor=0u;}
  }
  if(!roam_late || (LY_REG>=(enemies_alive ? 48u : 80u) && LY_REG<144u)) return;
  /* Bounded inspection, never a whole-room scan during a game frame. */
  /* Resting office turns reconcile the whole backing board promptly;
   * retain the existing scanline cutoff and moving/combat/world allowance. */
  probes=enemies_alive ? 0u : (!(joy&(J_LEFT|J_RIGHT|J_UP|J_DOWN)) && pl_state==PS_FREE &&
    !world_on && stage_type==SG_PUZZLE) ? 4u : 2u;
  /* Backpressure pauses inspection instead of permanently skipping a slab
   * while the flight queue is full. Urgent approaching floor is independent. */
  for(k=0u;k!=probes && roam_left && (uint8_t)(q_n+n_flying)<12u;k++) {
    /* A probe can touch a banked structure. Recheck the deadline inside the
     * scan instead of admitting six probes on its final available line. */
    if(video_epoch!=frame_epoch || (LY_REG>=(enemies_alive ? 48u : 64u) && LY_REG<144u))break;
    roam_left--;
    i=roam_scan;if(++roam_scan==EG_N) roam_scan=0u;
    roam_probe(i);
  }
}

/* --- drawing --- */
static void draw(uint8_t i) {
  draw_cell(i);
  budget--;
}
static void mark(uint8_t i);
static void mark_void(uint8_t n) {
  if (!look_here(n) || look_void_cell(n)) mark(n);
}
/* a cell came or went: it and the void under it (which shows an underside only below ground) now; the void beside
 * it and diagonally under it (B2: side faces, the lip's corners) with what is left of a frame's writes */
static void settle(uint8_t i) {
  uint8_t b, x = EG_X(i);
  draw(i);
  if (x) mark_void((uint8_t)(i - 1u));
  if (x != EG_W - 1u) mark_void((uint8_t)(i + 1u));
  if (EG_Y(i) < EG_H - 1u) {
    b = (uint8_t)(i + EG_W);
    if (!look_here(b) || look_void_cell(b)) {
      if(roam_on && game_mode==MODE_PLAY) mark(b);else draw(b);
    }
    if (x) mark_void((uint8_t)(b - 1u));
    if (x != EG_W - 1u) mark_void((uint8_t)(b + 1u));
  }
}

static void sprite_pair(uint8_t k, int16_t wx, int16_t wy, uint8_t tile, uint8_t prop, uint8_t spread) {
  uint8_t a = (uint8_t)(LOOK_OAM_TOP - (k << 1)), b = (uint8_t)(a - 1u);
  int16_t sx = wx - (int16_t)cam_x + 8, sy = wy - (int16_t)cam_y + 16;
  if ((uint16_t)(sy - 1) >= 159u || (uint16_t)(sx + 15) >= 191u) {
    hide_card(k);
    return;
  }
  oam_card_pair(a, b, (uint8_t)(sx - spread), (uint8_t)(sx + 8 + spread), (uint8_t)sy, tile, prop);
}

static uint8_t is_wallish(uint8_t m) {
  uint8_t p = (uint8_t)(meta_pal[m] & 7u);
  return (uint8_t)(p == LOOK_PAL_WALL || p == LOOK_PAL_HUD);
}

/* 10 sprites a line: the player's two layers take 4 on its 16 lines, so 3 cards may share a line there, 5 elsewhere
 * (this counts every card whose path overlaps, which is stricter than per line) */
static uint8_t band_ok(int16_t top, int16_t bot) {
  uint8_t k, n = 0;
  int16_t py = (int16_t)pl_y;
  for (k = 0; k != LOOK_MAX_CARDS; k++)
    if (c_kind[k] && c_top[k] < bot && c_bot[k] > top) n++;
  return (uint8_t)(n < ((top < py + 10 && bot > py - 18) ? 3u : 5u));
}

static uint8_t launch(uint8_t i) {
  uint8_t k,h,cached=0u,fall=(uint8_t)(look_here(i)!=0u);
  int16_t y=(int16_t)((uint16_t)EG_Y(i)<<4),top=y-(fall ? 0 : 16),bot=y+16+(fall ? 38 : 1);
  /* Seven reusable OBJ cards. The eighth slot remains available to a real
   * fragile-floor break, which uses its resident bank-0 art. */
  if(!band_ok(top,bot))return 0u;
  if(!fall)look_present[i>>3]|=BIT(i);
  h=cell_meta(i);
  if(!fall)look_present[i>>3]&=(uint8_t)~BIT(i);
  /* Reuse a resident card before uploading another 64-byte texture. Ground
   * repeatedly uses the same small set; the seven slots are also its cache. */
  for(k=0u;k<LOOK_MAX_CARDS-1u;k++)if(!c_kind[k] && c_meta[k]==h) {cached=1u;break;}
  if(!cached)for(k=0u;k<LOOK_MAX_CARDS-1u;k++)if(!c_kind[k])break;
  if(k==LOOK_MAX_CARDS-1u || ((!cached || !roam_on) && budget<2u))return 0u;
  if(!cached) {
    /* A stationary quiet room can finish one bounded cold-card upload after
     * heir preparation. Moving, attacking, combat and world turns keep the
     * original cutoff; a cold rim card cannot lock a resting queue forever. */
    if(roam_on && game_mode==MODE_PLAY && LY_REG>=
       (!(joy&(J_LEFT|J_RIGHT|J_UP|J_DOWN)) && pl_state==PS_FREE && !world_on &&
        !enemies_alive && stage_type==SG_PUZZLE ? 32u : 12u) && LY_REG<144u)return 0u;
    look_art_card(h,k);budget=0u;
  }
  c_kind[k]=fall ? K_FALL : K_DROP;c_cell[k]=i;c_meta[k]=h;c_t[k]=0u;
  c_top[k]=top;c_bot[k]=bot;in_flight[i>>3]|=BIT(i);n_flying++;
  if(fall) {
    /* Cosmetic departures do not wait in a crack state for a second BG
     * transaction. Remove the grounded card and start its downward motion
     * together, while it is still visible at the trailing edge. */
    look_present[i>>3]&=(uint8_t)~BIT(i);
    if(!cached) {budget=2u;settle(i);budget=0u;}else settle(i);
  }
  /* Incoming cells already show void. Their flight needs no placeholder
   * rewrite; the real map/art commits only when this same card settles. */
  return 1u;
}

static void finish(uint8_t k) {
  uint8_t i=c_cell[k];
  in_flight[i>>3]&=(uint8_t)~BIT(i);
  if(c_kind[k]==K_DROP && !c_brk[k])
    roam_hidden[i>>3]&=(uint8_t)~BIT(i);
  if (c_brk[k]) {
    c_brk[k] = 0;c_meta[k]=255u;
    n_break--;
  }
  c_kind[k] = K_FREE;
  n_flying--;
  hide_card(k);
}
void look_stream_void(uint8_t i) BANKED {
  uint8_t k,j,keep=0u;
  if(i<EG_N) {
    for(k=0u;k<LOOK_MAX_CARDS;k++)if(c_kind[k] && c_cell[k]==i)finish(k);
    for(k=0u;k<q_n;k++) {j=q[(uint8_t)(q_head+k)];if(j!=i)q[(uint8_t)(q_head+keep++)]=j;}
    q_n=keep;queued[i>>3]&=(uint8_t)~BIT(i);
    if(cell_type(i)==T_PIT)look_present[i>>3]|=BIT(i);
    else {look_present[i>>3]&=(uint8_t)~BIT(i);roam_hidden[i>>3]|=BIT(i);}
    look_busy_n=(uint8_t)(q_n+n_flying);
  }
  draw_meta(i,M_VOID_A);
}
static void incoming_pits(uint8_t i) NONBANKED NAKED {
  i;
  __asm
    push bc
    push de
    ld c,a
    ld e,a
    ld d,#0
    ld hl,#_room
    add hl,de
    ld b,#0
    ld a,(hl+)
    and a,#31
    cp a,#9
    jr nz,1$
    set 0,b
  1$:
    ld a,(hl+)
    and a,#31
    cp a,#9
    jr nz,2$
    set 1,b
  2$:
    ld a,(hl+)
    and a,#31
    cp a,#9
    jr nz,3$
    set 2,b
  3$:
    ld a,(hl)
    and a,#31
    cp a,#9
    jr nz,4$
    set 3,b
  4$:
    bit 2,c
    jr z,5$
    swap b
  5$:
    ld a,c
    rrca
    rrca
    rrca
    and a,#31
    ld e,a
    ld hl,#_look_present
    add hl,de
    ld a,(hl)
    or a,b
    ld (hl),a
    ld hl,#_roam_hidden
    add hl,de
    ld a,b
    cpl
    and a,(hl)
    ld (hl),a
    pop de
    pop bc
    ret
  __endasm;
}
/* Portable statement of the native leaf above, checked by the input harness
 * against incoming world materials after every completed camera transfer. */
#if 0
static void incoming_pits_reference(uint8_t i) {
  const uint8_t *p=room.c+i;uint8_t mask=0u,j=i>>3;
  if((p[0]&T_MASK)==T_PIT)mask|=1u;
  if((p[1]&T_MASK)==T_PIT)mask|=2u;
  if((p[2]&T_MASK)==T_PIT)mask|=4u;
  if((p[3]&T_MASK)==T_PIT)mask|=8u;
  if(i&4u)mask<<=4;
  look_present[j]|=mask;roam_hidden[j]&=(uint8_t)~mask;
}
#endif
/* Retained card spans move by exactly 64 pixels. A near leaf keeps the two
 * word updates out of the indexed queue loop and uses no additional WRAM. */
typedef char card_span_leaf_uses_eight_slots[(LOOK_MAX_CARDS==8u) ? 1 : -1];
static void shift_spans(int8_t dy) NAKED {
  dy;
  __asm
    push bc
    push de
    bit 7,a
    ld de,#0xFFC0
    jr z,1$
    ld de,#0x0040
  1$:
    ld c,#8
    ld hl,#_c_kind
  2$:
    ld a,(hl+)
    or a,a
    jr z,3$
    push hl
    push bc
    ld a,#8
    sub a,c
    add a,a
    ld l,a
    ld h,#0
    push hl
    ld bc,#_c_top
    add hl,bc
    ld a,(hl)
    add a,e
    ld (hl+),a
    ld a,(hl)
    adc a,d
    ld (hl),a
    pop hl
    ld bc,#_c_bot
    add hl,bc
    ld a,(hl)
    add a,e
    ld (hl+),a
    ld a,(hl)
    adc a,d
    ld (hl),a
    pop bc
    pop hl
  3$:
    dec c
    jr nz,2$
    pop de
    pop bc
    ret
  __endasm;
}
void look_shift_view(int8_t dx,int8_t dy) BANKED {
  uint8_t k,i,keep=0u;
  /* Each card is tested in its old coordinates. Recycle leaving slots and
   * rebase retained slots without changing material or object state. */
  for(k=0u;k<LOOK_MAX_CARDS;k++)if(c_kind[k]) {
    i=eg_view_rebase(c_cell[k],dx,dy);
    if(i>=EG_N)finish(k);
    else c_cell[k]=i;
  }
  if(dy)shift_spans(dy);
  for(k=0u;k<q_n;k++) {
    i=eg_view_rebase(q[(uint8_t)(q_head+k)],dx,dy);
    if(i<EG_N)q[(uint8_t)(q_head+keep++)]=i;
  }
  q_n=keep;
  eg_view_bits(look_present,dx,dy,0u);eg_view_bits(roam_hidden,dx,dy,1u);eg_view_bits(redraw,dx,dy,1u);
  /* Membership is a spatial bit field too. Shift the existing pools instead
   * of clearing and rebuilding every queued/card membership individually. */
  eg_view_bits(queued,dx,dy,0u);eg_view_bits(in_flight,dx,dy,0u);
  if(dx) {
    i=dx>0 ? 12u : 0u;k=14u;
    do {incoming_pits(i);i=(uint8_t)(i+16u);}while(--k);
  } else {
    i=dy>0 ? 160u : 0u;k=16u;
    do {incoming_pits(i);i=(uint8_t)(i+4u);}while(--k);
  }
  rd_any=1u;rd_cursor=0u;rd_left=28u;roam_scan=0u;roam_left=EG_N;roam_x=cam_x;roam_y=cam_y;
  look_busy_n=(uint8_t)(q_n+n_flying);
}

/* --- fragile floors: the break (src/look.h) --- */
static void step_card(uint8_t k);
static uint8_t roam_bg_time(void) {
  uint8_t ly=LY_REG;
  if(!roam_on || game_mode!=MODE_PLAY) return 1u;
  if(video_epoch!=frame_epoch) return 0u;
  return (uint8_t)(roam_late ? ly<(world_on ? 100u : 80u) : (ly>=144u || ly<40u));
}
static uint8_t prepare_time(void) {
  uint8_t ly=LY_REG;
  /* A single candidate can include metadata, a cache lookup and a BG commit.
   * Stop admitting that work early enough for an attack plus wave completion.
   * Quiet streamed worlds retain their measured wider preparation window. */
  return (uint8_t)(ly>=144u || ly<(world_on ? 24u : 8u));
}
static const int8_t wobble[8] = {1, -1, 1, -1, 1, 0, -1, 0};
#define WOBBLE_FRAMES 8u
#define SHATTER_FRAMES 16u

static void spr1(uint8_t a, int16_t wx, int16_t wy, uint8_t tile, uint8_t prop) {
  int16_t sx = wx - (int16_t)cam_x + 8, sy = wy - (int16_t)cam_y + 16;
  if ((uint16_t)(sy - 1) >= 159u || (uint16_t)(sx - 1) >= 167u) {
    move_sprite(a, 0, 0);
    return;
  }
  oam_card_pair(a, a, (uint8_t)sx, 0u, (uint8_t)sy, tile, prop);
}

static void step_break(uint8_t k, int16_t wx, int16_t wy, uint8_t tile) {
  uint8_t t = c_t[k], a = (uint8_t)(LOOK_OAM_TOP - (k << 1)), glassy = (uint8_t)(c_brk[k] >= LOOK_FRAG_GLASS);
  uint8_t dy, d;
  if (c_kind[k] == K_WOBBLE) {
    if (t >= WOBBLE_FRAMES) {
      c_t[k] = 0;
      if (glassy) {
        c_kind[k] = K_SHATTER;
        puff((uint8_t)(wx + 8), (uint8_t)(wy + 6), 6u, ST_SPARK);
      } else {
        c_kind[k] = K_FALL; /* the collapse's own fall: split, drift apart, darken, flicker out */
        puff((uint8_t)(wx + 8), (uint8_t)(wy + 10), 6u, ST_PUFF);
      }
      step_card(k);
      return;
    }
    /* it shifts under the feet; a darker blink on the 3rd and 6th frames */
    sprite_pair(k, wx + wobble[t], wy, tile, (uint8_t)((t == 2u || t == 5u) ? 7u : 6u), 0);
    c_t[k]++;
    return;
  }
  /* K_SHATTER: two shard sprites burst apart and fall, a second sparkle; glitched glass stutters as it goes */
  if (t >= SHATTER_FRAMES) {
    finish(k);
    return;
  }
  if (t == 2u) puff((uint8_t)(wx + 3), (uint8_t)(wy + 11), 6u, ST_SPARK);
  if (c_brk[k] == LOOK_FRAG_GLITCH && (t & 1u)) {
    hide_card(k);
    c_t[k]++;
    return;
  }
  dy = fall_dy[t < FALL_FRAMES ? t : FALL_FRAMES - 1u];
  d = (uint8_t)(t + (t >> 1));
  spr1(a, wx - (int16_t)d, wy + (int16_t)(dy >> 1), LOOK_SHARD, (uint8_t)(t < 8u ? 6u : 7u));
  spr1((uint8_t)(a - 1u), wx + 8 + (int16_t)d, wy + (int16_t)dy - 2, (uint8_t)(LOOK_SHARD + 2u),
       (uint8_t)((t < 6u ? 6u : 7u) | S_FLIPX));
  c_t[k]++;
}

static void step_card(uint8_t k) {
  uint8_t i = c_cell[k], t = c_t[k], kind = c_kind[k];
  uint8_t tile = c_brk[k] ? meta_tile[c_meta[k]][0] : (k<3u ? (uint8_t)(4u+(k<<2)) : (uint8_t)(100u+(k<<2))), prop;
  int16_t wx = (int16_t)((uint16_t)EG_X(i) << 4), wy = (int16_t)((uint16_t)EG_Y(i) << 4);
  if(kind==K_FALL && roam_on && !c_brk[k] && (roam_hidden[i>>3]&BIT(i)) && roam_wants(i))
    {kind=c_kind[k]=K_DROP;t=c_t[k]=0u;}
  if (kind >= K_WOBBLE) {
    step_break(k, wx, wy, tile);
    return;
  }
  if (kind == K_FALL) {
    if (t >= FALL_FRAMES) {
      finish(k);
      return;
    }
    if (c_brk[k] && t >= 9u && (t & 1u)) {
      hide_card(k); /* the last frames flicker out */
    } else {
      prop = c_brk[k] ? (uint8_t)(t < 5u ? 6u : 7u) : (uint8_t)(S_BANK|(meta_pal[c_meta[k]]&LOOK_META_FLIP)|(is_wallish(c_meta[k]) ? 7u : 6u));
      sprite_pair(k, wx, wy + fall_dy[t], tile, prop, c_brk[k] ? (uint8_t)(t >> 2) : 0u);
    }
    c_t[k]++;
    return;
  }
  /* assembly */
  if (t >= EASE_FRAMES) {
    if (budget < (roam_on ? 1u : 2u) || !roam_bg_time()) {
      sprite_pair(k,wx,wy,tile,(uint8_t)(S_BANK|(meta_pal[c_meta[k]]&LOOK_META_FLIP)|(is_wallish(c_meta[k]) ? 7u : 6u)),0u);
      return;
    }
    look_present[i >> 3] |= BIT(i);
    settle(i);
    finish(k);
    return;
  }
  prop = (uint8_t)(S_BANK|(meta_pal[c_meta[k]]&LOOK_META_FLIP)|(is_wallish(c_meta[k]) ? 7u : 6u));
  wy += ease_in[t];
  sprite_pair(k, wx, wy, tile, prop, 0);
  c_t[k]++;
}

/* --- ambient palette life (office: the light buzz; every biome: the screens' and tokens' shimmer, and the open
 * door's pulse: its light breathes with the HUD's accent, so the HUD's token and charge glints pulse with it) --- */
static uint8_t hum_on;
static void ambient(void) {
  uint8_t f = (uint8_t)frame_count;
  if ((f & 15u) == 0u) set_bkg_palette_entry(LOOK_PAL_OBJ, 2, amber[(f >> 4) & 3u]);
  if (door_open && (f & 7u) == 0u) {
    hum_on ^= 1u;
    set_bkg_palette_entry(LOOK_PAL_HUD, 2, hum[hum_on]);
  } else if (!door_open && hum_on) {
    hum_on = 0;
    set_bkg_palette_entry(LOOK_PAL_HUD, 2, hum[0]);
  }
  if (look_ambient_kind[biome_now] != 1u) return;
  if (flick_t) {
    if (!--flick_t) set_bkg_palette(LOOK_PAL_FLOOR, 1, &pal_bg[LOOK_PAL_FLOOR * 4u]);
    else if (flick_t == 3u) set_bkg_palette(LOOK_PAL_FLOOR, 1, dim0); /* a double buzz */
    else if (flick_t == 4u) set_bkg_palette(LOOK_PAL_FLOOR, 1, &pal_bg[LOOK_PAL_FLOOR * 4u]);
    return;
  }
  if (!--flick_next) {
    set_bkg_palette(LOOK_PAL_FLOOR, 1, dim0);
    flick_t = (lk_rnd() & 1u) ? 6u : 2u;
    flick_next = (uint8_t)(90u + (lk_rnd() & 127u));
  }
}

static void mark(uint8_t i) {
  redraw[i >> 3] |= BIT(i);
  rd_any = 1u;rd_left=EG_N/8u;
}
void look_redraw(uint8_t i) BANKED { mark(i); }

static uint8_t is_glitch(uint8_t m) { return (uint8_t)(m >= M_GLITCH0 && m <= M_GLITCH2); }

void look_skin_set(uint8_t i, uint8_t m) BANKED {
  world_touch(i);
  look_skin[i] = m == LOOK_SKIN_NONE ? 0u : (uint8_t)(m + 1u);
  if (m != LOOK_SKIN_NONE && is_glitch(m)) {
    gl_any = 1u;
    look_skin[i] = (uint8_t)(M_GLITCH0 + 1u + look_glitch_phase);
  }
  mark(i);
}

static void glitch_show(uint8_t ph) {
  uint8_t i = 0, sk;
  if (ph == look_glitch_phase) return;
  look_glitch_phase = ph;
  do {
    sk = look_skin[i];
    if (sk > M_GLITCH0 && sk <= M_GLITCH2 + 1u) {
      look_skin[i] = (uint8_t)(M_GLITCH0 + 1u + ph);
      world_touch(i);
      mark(i);
    }
  } while (++i != (uint8_t)EG_N);
}
void look_glitch_beat(uint8_t phase) BANKED { glitch_show(phase); }

/* the beat: solid, flickering (16), gone, flickering (16); a flicker blinks the card every 4 frames */
static void glitch_tick(void) {
  uint8_t len = (gl_step & 1u) ? 16u : look_glitch_period, ph;
  if (++gl_t >= len) {
    gl_t = 0;
    gl_step = (uint8_t)((gl_step + 1u) & 3u);
  }
  if (gl_step == 0u) ph = 0;
  else if (gl_step == 2u) ph = 2u;
  else ph = (gl_t & 4u) ? (gl_step == 1u ? 0u : 2u) : 1u;
  glitch_show(ph);
}

void look_break_tile(uint8_t i, uint8_t kind) BANKED {
  uint8_t k, m, p, y;
  if (!on || !look_here(i) || look_void_cell(i) || (queued[i >> 3] & BIT(i))) return;
  for (k = 0; k != LOOK_MAX_CARDS; k++)
    if (c_kind[k] && c_cell[k] == i) return;
  m = cell_meta(i);
  if (m == M_PIT) return;
  if (!kind)
    kind = is_glitch(m) ? LOOK_FRAG_GLITCH
                        : (m == M_GLASS ? LOOK_FRAG_GLASS : ((m == M_CEIL0 || m == M_CEIL1) ? LOOK_FRAG_CEIL : LOOK_FRAG_CARPET));
  sfx(kind == LOOK_FRAG_CARPET ? SFX_TEAR
                               : (kind == LOOK_FRAG_CEIL ? SFX_CRUNCH : (kind == LOOK_FRAG_GLASS ? SFX_SHATTER : SFX_ZAP)));
  /* the card that goes: the cell's own when it lives in bank 0, else the kind's last stage */
  if (!(meta_pal[m] & LOOK_META_BANK0) || is_glitch(m))
    m = kind == LOOK_FRAG_CARPET ? M_CARPET2 : (kind == LOOK_FRAG_CEIL ? M_CEIL1 : (kind == LOOK_FRAG_GLASS ? M_GLASS : M_GLITCH0));
  look_skin[i] = (uint8_t)(M_PIT + 1u);
  world_touch(i);
  redraw[i >> 3] &= (uint8_t)~BIT(i);
  draw_meta(i, M_PIT);
  for (k = 0; k != LOOK_MAX_CARDS; k++)
    if (!c_kind[k]) break;
  if (k == LOOK_MAX_CARDS) return; /* no room in the air: the pit, at once */
  p = (uint8_t)((meta_pal[m] & 7u) << 2);
  for (y = 0; y != 4u; y++) {
    brk_obj[y] = pal_bg[p + y];
    brk_obj[4u + y] = mix(pal_bg[p + y], pal_bg[LOOK_PAL_VOID * 4u + 1u], 8u);
  }
  obj_mode = 0xfeu; /* rewrite OBJ 6/7 on the next tick */
  y = EG_Y(i);
  c_kind[k] = K_WOBBLE;
  c_cell[k] = i;
  in_flight[i>>3]|=BIT(i);
  c_meta[k] = m;
  c_t[k] = 0;
  c_brk[k] = kind;
  c_top[k] = (int16_t)((uint16_t)y << 4);
  c_bot[k] = (int16_t)(((uint16_t)y << 4) + 16u + 38u);
  n_flying++;
  n_break++;
  look_busy_n = (uint8_t)(q_n + n_flying);
}

static void look_update(uint8_t prepare);
void look_anchor(uint8_t i) BANKED {
  uint8_t k;
  if(i>=EG_N || look_here(i) || !(roam_hidden[i>>3]&BIT(i))) return;
  look_present[i>>3]|=BIT(i);roam_hidden[i>>3]&=(uint8_t)~BIT(i);
  queued[i>>3]&=(uint8_t)~BIT(i);
  for(k=0u;k!=LOOK_MAX_CARDS;k++) if(c_kind[k] && c_cell[k]==i && !c_brk[k]) {finish(k);break;}
  draw_cell(i);
  if(EG_Y(i)<EG_H-1u) mark((uint8_t)(i+EG_W));
  if(EG_X(i)) mark((uint8_t)(i-1u));
  if(EG_X(i)<EG_W-1u) mark((uint8_t)(i+1u));
}
static uint8_t anchor_at(uint8_t x,uint8_t y) {
  uint8_t i;
  if(x>=EG_W || y>=EG_H)return 0u;
  i=EG_IDX(x,y);
  if(look_here(i) || !(roam_hidden[i>>3]&BIT(i)))return 0u;
  look_anchor(i);return 1u;
}
static uint8_t ground_ahead(uint8_t essential) {
  int16_t x=pl_x,y=pl_y;int8_t dx=0,dy=0;uint8_t n=0u,k,c,x0,x1,y0,y1,vertical,limit,wide;
  /* A blink travels at most 6.5px/tick. Prepare the next 7px step's complete
   * 12x12 foot box, including diagonal corners and released rolls. A farther
   * point can skip the two adjacent cells crossed by a diagonal step. */
  /* Sprint also presents as PS_ROLL, but follows the held direction rather
   * than the last evade vector (which may still be zero). Only an actual
   * evade retains its vector after the direction is released. */
  if(pl_state==PS_ROLL && pin.move==EG_MV_DODGE) {dx=roll_dx;dy=roll_dy;}
  else {
    if((joy&(J_LEFT|J_RIGHT))!= (J_LEFT|J_RIGHT))dx=joy&J_RIGHT ? 1 : joy&J_LEFT ? -1 : 0;
    if((joy&(J_UP|J_DOWN))!= (J_UP|J_DOWN))dy=joy&J_DOWN ? 1 : joy&J_UP ? -1 : 0;
  }
  x+=dx*7;y+=dy*7;
  x0=(uint8_t)((x-6)>>4);x1=(uint8_t)((x+5)>>4);
  y0=(uint8_t)((y-6)>>4);y1=(uint8_t)((y+5)>>4);
  n+=anchor_at(x0,y0);
  if(n<2u && x1!=x0)n+=anchor_at(x1,y0);
  if(n<2u && y1!=y0)n+=anchor_at(x0,y1);
  if(n<2u && y1!=y0 && x1!=x0)n+=anchor_at(x1,y1);
  if(n || essential || video_epoch!=frame_epoch)return n;
  if(enemies_alive>=2u && (pl_state==PS_LASH || pl_state==PS_SPIN))return n;
  if(LY_REG<144u && LY_REG>=(world_on ? 24u : 32u))return 0u;
  /* Easing continues after a turn. Prepare the camera's residual axis too. */
  if(cam_x<96u && pl_x>cam_x+80u)dx=1;
  else if(cam_x && pl_x<cam_x+80u)dx=-1;
  if(cam_y<96u && pl_y>cam_y+64u)dy=1;
  else if(cam_y && pl_y<cam_y+64u)dy=-1;
  if(!dx && !dy)return 0u;
  /* A slow/busy card must land before it leaves the edge band. This sweep is
   * a half slab INSIDE the approaching screen edge, after its visible entrance,
   * rather than two slabs outside the screen as in the previous release. */
  vertical=(uint8_t)(dy && (!dx || (frame_count&1u)));
  limit=vertical ? 11u : 9u;
  if(near_cursor>=limit)near_cursor=0u;
  x=vertical ? cam_x>>4 : ((uint16_t)cam_x+(dx>0 ? 152u : 8u))>>4;
  y=vertical ? ((uint16_t)cam_y+(dy>0 ? 120u : 8u))>>4 : cam_y>>4;
  /* Quiet puzzles can ease just as fast as streamed worlds. Inspect their
   * complete edge and land up to two slabs; a single repair cannot cover all
   * nine rows during a fast reversal. Combat retains its smaller allowance. */
  wide=(uint8_t)(world_on || (stage_type==SG_PUZZLE && !enemies_alive));
  for(k=0u;k<(wide ? limit : 4u) && n<(wide ? 2u : 1u);k++) {
    c=near_cursor;if(++near_cursor==limit)near_cursor=0u;
    n+=anchor_at((uint8_t)(x+(vertical ? c : 0u)),(uint8_t)(y+(vertical ? 0u : c)));
  }
  /* Swept feet remain independent of the bounded cosmetic sweep. */
  return n;
}

void look_frame_begin(void) BANKED {
  uint8_t k,waiting;
  if (!on) return;
  /* palettes first, while VBlank lasts (a break in the air takes OBJ 6/7 for its card's colours) */
  k = n_break ? 3u : look_busy_n ? (roam_on ? 1u : obj_want) : 0u;
  if (k && k != obj_mode) {
    set_sprite_palette(6, 2, k == 3u ? brk_obj : (k == 1u ? obj_pal[0] : obj_pal[2]));
    obj_mode = k;
  } else if (obj_mode && !look_busy_n) {
    set_sprite_palette(6, 2, &pal_sp[24]);
    obj_mode = obj_want = 0;
  }
  waiting=(uint8_t)(world_on && game_mode==MODE_PLAY && world_work_pending());
  /* Upload cosmetic palettes/cards while there is an early VRAM window.
   * Office combat cannot pay their STAT waits after a periodic clock/HUD
   * update. Quiet generated worlds have their separate wider allowance. */
  if(!waiting && (!roam_on || game_mode!=MODE_PLAY || LY_REG>=144u || LY_REG<(world_on ? 40u : 8u))) {
    ambient();
    if (game_mode == MODE_PLAY && !look_busy_n) items_tick();
  }
  if(roam_on && game_mode==MODE_PLAY) {
    /* Incoming terrain must not starve behind actor rendering. Commit a
     * bounded ready card / launch here; animation stays after the actors. */
    roam_late=0u;
    k=ground_ahead(waiting);
    /* A multi-target swing can spend another thirty scanlines before the
     * complete actor list is drawn. Keep its speculative terrain admission
     * free; swept feet above and existing flying cards still advance. */
    if(k<2u && prepare_time() && grav_t!=1u && !push_t &&
       !(enemies_alive>=2u && (pl_state==PS_LASH || pl_state==PS_SPIN)) &&
       (stage_type==SG_PUZZLE || (frame_count&3u))) {
      /* Give generation its turn without freezing the visible rim. One
       * resident card may launch or settle; cold texture uploads still wait. */
      if(waiting)look_update(3u);
      else {if(!(frame_count&1u))look_roam_tick();look_update((uint8_t)(1u+k));}
    }
    roam_late=1u;
  }
}

static void look_update(uint8_t prepare) {
  uint8_t k, i, launches = 0, scan;
  if(!on) return;
  /* Gravity, obstacles and actor composition share the frame. Defer BG commits
   * on that heavy tick; moving/falling cards still animate, and a settled card
   * waits for the next tick's bounded two-cell commit. */
  budget = roam_late && roam_on && !prepare ? 0u : game_mode == MODE_PLAY ? (grav_t==1u || (roam_on && push_t) ? 0u : prepare>=2u ? 1u : 2u) : LOOK_COMMITS_PER_FRAME;
  if (look_dbg_kind) { /* the harness: 0x80 applies a poked skin, else a break of that kind */
    if (look_dbg_kind == 0x80u) look_skin_set(look_dbg_cell, (uint8_t)(look_skin[look_dbg_cell] - 1u));
    else look_break_tile(look_dbg_cell, look_dbg_kind);
    look_dbg_kind = 0;
  }
  if (!prepare && gl_any && look_glitch_period) glitch_tick();
  for (k = 0; k != LOOK_MAX_CARDS; k++)
    if (c_kind[k]) {
      if(prepare) {
        if(!prepare_time())break;
        if(budget>=1u && !c_brk[k]) {
          if(c_kind[k]==K_DROP && c_t[k]>=EASE_FRAMES) {
            step_card(k);
            if(budget<2u) {budget=0u;break;}
          }
        }
        continue;
      }
      /* Terrain is last in a combat frame. Keep its animation state for the
       * next tick when actors have used the remaining display time. */
      if(roam_late && roam_on && (video_epoch!=frame_epoch || (LY_REG>=118u && LY_REG<144u))) hide_card(k);
      else step_card(k);
    }
  /* the wave: off-screen cells at once, cards on screen as there is room for them */
  scan = prepare ? 4u : 12u;
  while (q_n && (budget>=2u || (prepare==3u && budget)) && scan-- && roam_bg_time() && (!prepare || prepare_time())) {
    i = q[q_head];
    if(!(queued[i>>3]&BIT(i))) {q_head++;q_n--;continue;}
    if(roam_on && (roam_hidden[i>>3]&BIT(i)) && look_here(i) && roam_wants(i)) {
      roam_hidden[i>>3]&=(uint8_t)~BIT(i);queued[i>>3]&=(uint8_t)~BIT(i);q_head++;q_n--;continue;
    }
    if (!on_screen(i) && !(roam_on && roam_wants(i))) {
      if(roam_on) {
        /* A queued arrival abandoned by a reversal stays hidden. */
        look_present[i>>3]&=(uint8_t)~BIT(i);roam_hidden[i>>3]|=BIT(i);
      } else if (look_here(i)) look_present[i >> 3] &= (uint8_t)~BIT(i);
      else look_present[i >> 3] |= BIT(i);
      settle(i);
    } else {
      /* the head, or one of the next few if the head's band is full (the wave keeps its order, roughly) */
      uint8_t j, ii = 0, found = 0;
      if (launches == (prepare==3u ? 1u : prepare ? 2u : 3u)) break;
      for (j = 0; j != (prepare ? 2u : 4u) && j < q_n && roam_bg_time() && (!prepare || prepare_time()); j++) {
        ii = q[(uint8_t)(q_head + j)];
        if ((on_screen(ii) || (roam_on && roam_wants(ii))) && look_here(ii) == look_here(i) && launch(ii)) {
          found = 1u;
          break;
        }
      }
      if (!found) {
        /* A cold visible head must not block departures behind it forever.
         * Rotate one retry on resting office turns; no extra upload or draw. */
        if(!(joy&(J_LEFT|J_RIGHT|J_UP|J_DOWN)) && pl_state==PS_FREE &&
           roam_on && !world_on && !enemies_alive && stage_type==SG_PUZZLE && q_n>1u) {
          q[(uint8_t)(q_head+q_n)]=i;q_head++;
        }
        break;
      }
      q[(uint8_t)(q_head + j)] = i;
      i = ii;
      launches++;
    }
    queued[i >> 3] &= (uint8_t)~BIT(i);
    q_head++;
    q_n--;
    if(prepare && !budget)break;
  }
  /* skins and stages changed: redraw with what is left of the budget */
  if (rd_any && budget && roam_bg_time() && (!prepare || prepare_time())) {
    /* A queued cell can consume a dirty bit without drawing, so the BG budget
     * alone does not bound this walk. Inspect four bytes / two marked cells
     * at most, retaining the cursor until every pending byte is visited. */
    scan=4u;launches=prepare ? 1u : 2u;
    while(scan-- && rd_left && budget && roam_bg_time() && (!prepare || prepare_time())) {
      uint8_t bit=0u;
      k=rd_cursor;
      if(redraw[k]) {
        while(!(redraw[k]&look_bits[bit])) bit++;
        redraw[k]&=(uint8_t)~look_bits[bit];i=(uint8_t)((k<<3)|bit);
        if(!(queued[k]&look_bits[bit])) draw(i);
        if(!redraw[k]) {if(++rd_cursor==EG_N/8u)rd_cursor=0u;rd_left--;}
        if(!--launches)break;
      } else {if(++rd_cursor==EG_N/8u)rd_cursor=0u;rd_left--;}
    }
    rd_any=(uint8_t)(rd_left!=0u);
  }
  look_busy_n = (uint8_t)(q_n + n_flying);
}

void look_frame_end(void) BANKED {
  roam_late=1u;
  /* Actors already hide the late card slots. Keep their animation/queue
   * state intact when only the VBlank publication margin remains. */
  if(roam_on && (video_epoch!=frame_epoch || LY_REG>=108u))return;
  look_update(0u);
}
void look_tick(void) BANKED {
  roam_late=0u;look_frame_begin();
  /* Quiet-room work is still in the early phase. frame_begin leaves the
   * split combat path ready for its tail; do not inherit that zero budget. */
  roam_late=0u;look_update(0u);
  roam_late=1u; /* the later camera sweep may now reconcile trailing cells */
}

/* The controls scene stops physics but keeps presentation alive. A suspended
 * gravity/push turn must not reserve the BG budget forever. Its temporary
 * camera framing must not discover new roaming terrain either. */
void look_pause_tick(void) BANKED {
  uint8_t previous_mode=game_mode;
  game_mode=MODE_CLEAR;
  look_tick();
  game_mode=previous_mode;
  if(game_mode==MODE_PLAY && !look_busy_n)items_tick();
}
