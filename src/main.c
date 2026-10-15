/* EGRESS.EXE: power-on, frame pacing, the HUD split, palettes and text. Bank 0. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "egress.h"
#include "gen/art.h"
#include "clock.h"

uint8_t game_mode;
uint16_t run_seed;
uint8_t floor_no;
uint16_t floor_seed;
uint8_t floor_tier;
eg_room room;
uint16_t frame_count;
uint8_t joy, joy_prev, joy_pressed;
uint8_t dbg_god, dbg_no_enemies, dbg_floor;
uint16_t dbg_seed;
uint8_t hud_line = 144u;
uint16_t pal_bg[32], pal_sp[32];
uint8_t pal_dirty;

void input_poll(void) {
  joy_prev = joy;
  joy = joypad();
  joy_pressed = (uint8_t)(joy & (uint8_t)~joy_prev);
}

/* sprites switch off at hud_line (the HUD window starts there) and back on in VBlank. Only that one split: other
 * screens' LYC lines (the title's logo slices at 11-29 and its 144) are theirs, and leave the sprites alone. */
static void lcd_isr(void) NONBANKED {
  if (hud_line < 144u && LYC_REG == hud_line) LCDC_REG &= (uint8_t)~LCDCF_OBJON;
}
static void vbl_isr(void) NONBANKED {
  LCDC_REG |= LCDCF_OBJON;
  LYC_REG = hud_line;
}

void frame_wait(void) {
  vsync();
  frame_count++;
  if (!((uint8_t)frame_count & 63u)) clock_poll(); /* the clock counts VBlanks; any gap up to 18 minutes is exact */
}

static uint16_t scale(uint16_t c, uint8_t level) {
  uint8_t r = (uint8_t)(c & 31u), g = (uint8_t)((c >> 5) & 31u), b = (uint8_t)((c >> 10) & 31u);
  r = (uint8_t)(((uint16_t)r * level) >> 3);
  g = (uint8_t)(((uint16_t)g * level) >> 3);
  b = (uint8_t)(((uint16_t)b * level) >> 3);
  return (uint16_t)(r | ((uint16_t)g << 5) | ((uint16_t)b << 10));
}

/* write the live palettes at a fade level; call right after a frame_wait */
void pal_commit(uint8_t level) {
  static uint16_t buf[32];
  uint8_t i;
  if (level >= 8u) {
    set_bkg_palette(0, 8, pal_bg);
    set_sprite_palette(0, 8, pal_sp);
  } else {
    for (i = 0; i != 32u; i++) buf[i] = scale(pal_bg[i], level);
    set_bkg_palette(0, 8, buf);
    for (i = 0; i != 32u; i++) buf[i] = scale(pal_sp[i], level);
    set_sprite_palette(0, 8, buf);
  }
  pal_dirty = 0;
}

void fade_to(uint8_t from, uint8_t to) {
  while (from != to) {
    from = from < to ? (uint8_t)(from + 1u) : (uint8_t)(from - 1u);
    frame_wait();
    pal_commit(from);
    sound_tick();
    frame_wait();
    sound_tick();
  }
}

static const char font_order[] = BT_FONT_ORDER;
static uint8_t glyph_tile(char ch) {
  uint8_t i;
  if (ch >= 'a' && ch <= 'z' && ch != 'x') ch = (char)(ch - 32);
  for (i = 0; font_order[i]; i++)
    if (font_order[i] == ch) return (uint8_t)(BT_FONT + i);
  return BT_FONT;
}

void text_at(uint8_t win, uint8_t x, uint8_t y, const char *s, uint8_t pal) {
  uint8_t t, a = (uint8_t)(pal | 0x08u);
  while (*s) {
    t = glyph_tile(*s++);
    VBK_REG = 1;
    if (win)
      set_win_tiles(x, y, 1, 1, &a);
    else
      set_bkg_tiles(x, y, 1, 1, &a);
    VBK_REG = 0;
    if (win)
      set_win_tiles(x, y, 1, 1, &t);
    else
      set_bkg_tiles(x, y, 1, 1, &t);
    x++;
  }
}

void num_at(uint8_t win, uint8_t x, uint8_t y, uint16_t n, uint8_t digits, uint8_t pal) {
  char buf[6];
  uint8_t i = digits;
  buf[digits] = 0;
  while (i) {
    i--;
    buf[i] = (char)('0' + (uint8_t)(n % 10u));
    n /= 10u;
  }
  text_at(win, x, y, buf, pal);
}

void clear_map(uint8_t win, uint8_t pal) {
  static uint8_t row[32];
  uint8_t y;
  memset(row, (uint8_t)(pal | 0x08u), 32);
  VBK_REG = 1;
  for (y = 0; y != 32u; y++) {
    if (win)
      set_win_tiles(0, y, 32, 1, row);
    else
      set_bkg_tiles(0, y, 32, 1, row);
  }
  memset(row, 0, 32);
  VBK_REG = 0;
  for (y = 0; y != 32u; y++) {
    if (win)
      set_win_tiles(0, y, 32, 1, row);
    else
      set_bkg_tiles(0, y, 32, 1, row);
  }
}

void main(void) {
  if (_cpu != CGB_TYPE) {
    /* CGB only (the header says so); a DMG shows a blank screen */
    DISPLAY_ON;
    while (1) vsync();
  }
  cpu_fast();
  DISPLAY_OFF;
  SPRITES_8x16;
  STAT_REG = STATF_LYC;
  LYC_REG = 144u;
  CRITICAL {
    add_LCD(lcd_isr);
    add_VBL(vbl_isr);
  }
  set_interrupts(VBL_IFLAG | LCD_IFLAG);
  sound_init();
  game_mode = MODE_BOOT;
  clock_init(); /* the RTC build reads the chip once here and again after the boot: it must have ticked */
  keel_boot();
  clock_probe();
  while (1) {
    title_screen();
    play_run();
  }
}
