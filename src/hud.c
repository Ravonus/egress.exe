/* The floor's HUD in the window, in the HUD palette (LOOK_PAL_HUD: ink, pink, accent, white).
 *   row top+0  the rim: the charge bar on the left, a white-over-pink rim line across the rest
 *   row top+1  hearts, tokens, keycards, the floor
 * A message is a toast above the HUD: the window rises 8 px (row 0 is the message, centred) for as long as it shows,
 * so the rim always separates it from the counters. Banked (B2: ROM0 is for interrupts and hot paths): only the
 * VBlank copy and hud_msg's front stay in bank 0, the front copying the caller's string to WRAM before the switch.
 *
 * Every write goes to a shadow of the window's three rows. hud_tick hands the rows that changed to a VBlank handler,
 * which copies them into the window map and then moves the window if the toast opened or closed, so the rows and
 * the window's height change in the same VBlank. Written a tile at a time through set_win_tiles (attribute, then
 * tile, each waiting for VRAM) a toast cost ~120 scan lines of the frame; the VBlank copy is 20 bytes a row. The
 * attributes never change (every HUD tile is LOOK_PAL_HUD in VRAM bank 1): hud_init writes them once. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "stamina.h"
#include "stage.h" /* B2 */

#pragma bank 255

static uint8_t msg_t, top, shown_top = 0xffu, rows_dirty, vbl_on;
static uint8_t narr_t,shown_stamina;
static volatile uint8_t vbl_rows, vbl_move, vbl_top; /* for hud_vbl: rows to copy, the window's new place */
static uint8_t sh[3][20];   /* the window's rows 0..2, tiles */
static uint8_t glyph[64];   /* ' ' .. '_' -> font tile (BT_FONT_ORDER); lower case folds to upper, but 'x' */
static uint8_t glyph_x;     /* the font's own 'x' */
static void hud_vbl(void) NONBANKED;
#define HUD_Y 128u   /* the window's top without a toast */
#define TOAST_Y 120u /* with one */

static void hud_tile(uint8_t x, uint8_t y, uint8_t t) {
  if(sh[y][x]==t) return;
  sh[y][x] = t;
  rows_dirty |= (uint8_t)(1u << y);
}

static uint8_t glyph_of(char ch) {
  if (ch == 'x') return glyph_x;
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  return ((uint8_t)ch >= 32u && (uint8_t)ch < 96u) ? glyph[(uint8_t)ch - 32u] : BT_FONT;
}

static void hud_text(uint8_t x, uint8_t y, const char *s) {
  uint8_t *d = &sh[y][x];
  while (*s && x != 20u) {
    *d++ = glyph_of(*s++);
    x++;
  }
  rows_dirty |= (uint8_t)(1u << y);
}

/* n as `digits` digits (at most 3), by subtraction: SDCC's 8-bit divide is a library call per digit */
static void hud_num(uint8_t x, uint8_t y, uint8_t n, uint8_t digits) {
  uint8_t *d = &sh[y][x], h = 0, t = 0, z = glyph['0' - 32];
  while (n >= 100u) {
    n -= 100u;
    h++;
  }
  while (n >= 10u) {
    n -= 10u;
    t++;
  }
  if (digits == 3u) *d++ = (uint8_t)(z + h); /* the font's digits are consecutive, 0..9 */
  if (digits >= 2u) *d++ = (uint8_t)(z + t);
  *d = (uint8_t)(z + n);
  rows_dirty |= (uint8_t)(1u << y);
}

static void draw_n(void);   /* near: the public hud_draw/hud_charge are banked calls */
static void charge_n(void);
static void hud_layout(uint8_t toast) {
  uint8_t i;
  top = toast;
  memset(sh, BT_FONT, sizeof sh); /* BT_FONT is the space */
  rows_dirty = 7u;
  for (i = 5u; i != 20u; i++) sh[top][i] = BT_RIM;
  charge_n();
  draw_n();
}

void hud_init(void) BANKED {
  static const char order[] = BT_FONT_ORDER;
  uint8_t i, a[20];
  memset(glyph, BT_FONT, sizeof glyph);
  for (i = 0; order[i]; i++) {
    uint8_t ch = (uint8_t)order[i];
    if (ch == 'x') glyph_x = (uint8_t)(BT_FONT + i);
    else if (ch >= 32u && ch < 96u) glyph[ch - 32u] = (uint8_t)(BT_FONT + i);
  }
  memset(a, (uint8_t)(LOOK_PAL_HUD | 0x08u), 20);
  VBK_REG = 1;
  for (i = 0; i != 3u; i++) set_win_tiles(0, i, 20, 1, a);
  for(i=0u;i!=3u;i++) a[i]=(uint8_t)(LOOK_PAL_OBJ|0x08u);
  set_win_tiles(0,0u,3u,1u,a);
  VBK_REG = 0;
  msg_t = 0;
  narr_t=0;shown_stamina=255u;
  hud_layout(0);
  /* the first rows and the window's place now (the display may be off: no VBlank to wait for) */
  for (i = 0; i != 3u; i++) set_win_tiles(0, i, 20, 1, sh[i]);
  rows_dirty = 0;
  shown_top = 0;
  CRITICAL {
    vbl_rows = 0;
    vbl_move = 0;
  }
  move_win(7, HUD_Y);
  hud_line = HUD_Y;
  if (!vbl_on) {
    vbl_on = 1;
    CRITICAL { add_VBL(hud_vbl); }
  }
}

static char msg_buf[21];
void hud_msg_w(void) BANKED;
/* bank 0: the string may live in the caller's bank; a toast is at most 20 characters */
void hud_msg(const char *s) NONBANKED {
  uint8_t n = 0;
  while (s[n] && n != 20u) {
    msg_buf[n] = s[n];
    n++;
  }
  msg_buf[n] = 0;
  hud_msg_w();
}
void hud_msg_w(void) BANKED {
  const char *s = msg_buf;
  uint8_t n = 0;
  narr_t=0u;
  while (s[n]) n++;
  if (!n) {
    msg_t = 1u; /* closes on the next tick */
    return;
  }
  if (!top) hud_layout(1u);
  else memset(sh[0], BT_FONT, 20);
  hud_text((uint8_t)((20u - (n > 20u ? 20u : n)) >> 1), 0, s);
  msg_t = 150u;
}
uint8_t hud_narr_ready(void) BANKED {return (uint8_t)(!top && !msg_t && !narr_t);}
void hud_narrate(const char *s) BANKED {
  if(!hud_narr_ready()) return;
  hud_text(6u,0u,s);narr_t=180u;
}

/* VBlank: the rows hud_tick handed over, then the window's place (rows first: never the new height over old rows) */
static void hud_vbl(void) NONBANKED {
  uint8_t v, *base;
  if (vbl_rows) {
    v = VBK_REG;
    VBK_REG = 0;
    base = (uint8_t *)((LCDC_REG & LCDCF_WIN9C00) ? 0x9C00u : 0x9800u);
    if (vbl_rows & 1u) memcpy(base, sh[0], 20);
    if (vbl_rows & 2u) memcpy(base + 32, sh[1], 20);
    if (vbl_rows & 4u) memcpy(base + 64, sh[2], 20);
    VBK_REG=1u;
    base[0]=base[1]=base[2]=base[32]=base[33]=base[34]=base[64]=base[65]=base[66]=(uint8_t)(LOOK_PAL_HUD|0x08u);
    base+=(vbl_move ? vbl_top : shown_top) ? 32u : 0u;
    base[0]=base[1]=base[2]=(uint8_t)(LOOK_PAL_OBJ|0x08u);
    VBK_REG=v;
    vbl_rows=0;
  }
  if (vbl_move) {
    WX_REG = 7u;
    WY_REG = hud_line = vbl_top ? TOAST_Y : HUD_Y;
    LYC_REG = hud_line;
    vbl_move = 0;
  }
}

void hud_tick(void) BANKED {
  if(shown_stamina!=stamina.value) {shown_stamina=stamina.value;charge_n();}
  if(narr_t && !--narr_t) {uint8_t k;for(k=6u;k!=20u;k++) sh[top][k]=BT_RIM;rows_dirty|=(uint8_t)(1u<<top);}
  if (msg_t && !--msg_t) hud_layout(0);
  if (hud_dirty) draw_n();
  if (!rows_dirty && top == shown_top) return;
  CRITICAL {
    vbl_rows |= rows_dirty;
    if (top != shown_top) {
      vbl_top = top;
      vbl_move = 1;
    }
  }
  rows_dirty = 0;
  shown_top = top;
}

static void draw_n(void) {
  uint8_t i, n = (uint8_t)((pl_hp_max+1u) >> 1), hp = pl_hp, y = (uint8_t)(top + 1u);
  uint8_t *d = sh[y];
  if(pl_hp_max>10u) {
    /* Upgraded health still owns five cells; never overwrite the token count. */
    hud_num(0u,y,pl_hp,2u);sh[y][2]=glyph['/'-32];hud_num(3u,y,pl_hp_max,2u);
  } else {
    for(i=0u;i!=5u;i++) sh[y][i]=BT_FONT;
    for (i = 0; i != n; i++, hp = hp > 2u ? (uint8_t)(hp - 2u) : 0u) /* two halves a heart */
      *d++ = hp >= 2u ? BT_HEART2 : (hp ? BT_HEART1 : BT_HEART0);
  }
  sh[y][5] = BT_ICON_TOKEN;
  hud_num(6, y, tokens, 3);
  if (boss_floor) {
    sh[y][10] = sh[y][11] = sh[y][12] = sh[y][13] = BT_FONT;
  } else if (stage_hud[0]) { /* B2: the stage's counter */
    hud_text(10, y, stage_hud);
  } else {
    sh[y][10] = BT_ICON_KEY;
    hud_num(11, y, keys_held, 1);
    sh[y][12] = glyph['/' - 32];
    hud_num(13, y, room.keys, 1);
  }
  sh[y][16] = glyph['F' - 32];
  hud_num(17, y, floor_no, 2);
  rows_dirty |= (uint8_t)(1u << y);
  hud_dirty = 0;
}

static void charge_n(void) {
  uint8_t i, full = CHARGE_FULL;
  uint8_t lit=stamina.cap ? (uint8_t)((uint16_t)stamina.value*24u/stamina.cap) : 24u;
  for (i = 0; i != 3u; i++) {
    uint8_t n = lit > 8u ? 8u : lit;
    hud_tile(i, top, (uint8_t)(BT_BAR + n));
    lit=(uint8_t)(lit-n);
  }
  lit=pl_charge>=full ? 16u : (uint8_t)((uint16_t)pl_charge*16u/full);
  for(i=3u;i!=5u;i++) {uint8_t n=lit>8u ? 8u : lit;hud_tile(i,top,(uint8_t)(BT_BAR+n));lit-=n;}
}

void hud_draw(void) BANKED { draw_n(); }
void hud_charge(void) BANKED { charge_n(); }
