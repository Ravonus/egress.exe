/* The KEEL start-up, ported from crucible.exe's handheld (crucible.onkeel.io: rictus
 * ops/crucible-live-service-20261006/site/src/lib/boot.ts, the boot chime in src/lib/sound.ts, the mark
 * public/ui/keel-mark.png). Same art, colours, frames and chime; the tables come from proto/boot/keel_boot_ref.py.
 *
 * A light screen; the KEEL glyph over "KEEL" in the cartridge's 5x7 letters at 2x scrolls down from above the screen
 * to the centre in whole-pixel steps (1.5 s), lands with a two-note chime (C6 for 5 frames, then B6 for 44, pulse 1,
 * 50 % duty, NRx2 0x81 then 0xa3), holds 0.7 s, then dithers away into the game in 16 ordered 4x4 steps over 0.3 s.
 * The game under the dither is the title's first frame, which starts black and fades in.
 *
 * The scroll is SCY over a fixed map. The dither-away rebuilds the 43 cells it uses (the mask is the same in every
 * cell) during the frame and copies them with general-purpose VRAM DMA at the start of VBlank (43 blocks, ~3 lines),
 * so every step lands on its own frame. The LCD shows nothing on the frame it is switched on (as on hardware), so the
 * first boot frame is set up, the LCD is enabled, and frame 0 is the next one. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "../egress.h"
#include "keel_boot_data.h"

uint8_t screen_dark;
static const uint8_t bayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static uint8_t cells_raw[(KB_TILES + 1u) * 16u + 15u];
static uint8_t *cells; /* 16-byte aligned for VRAM DMA: cell 0 is the empty screen colour, then the logo */

/* the cells with every pixel whose Bayer value is under `cut` turned to colour 3 (the game's black) */
static void cut_cells(uint8_t cut) {
  uint8_t mask[4], r, x, m, *p;
  uint16_t i;
  for (r = 0; r != 4u; r++) {
    m = 0;
    for (x = 0; x != 8u; x++)
      if (bayer[(uint8_t)(r * 4u + (x & 3u))] < cut) m |= (uint8_t)(0x80u >> x);
    mask[r] = m;
  }
  memset(cells, 0, 16);
  memcpy(cells + 16, kb_tiles, KB_TILES * 16u);
  p = cells;
  for (i = 0; i != (KB_TILES + 1u) * 8u; i++) {
    m = mask[(uint8_t)i & 3u]; /* row (i & 7), and the logo's top sits on a multiple of 4 */
    *p++ |= m;
    *p++ |= m;
  }
}

static void chime_note(uint8_t n) {
  NR10_REG = 0;
  NR11_REG = kb_chime[n][2];
  NR12_REG = kb_chime[n][1];
  NR13_REG = kb_chime[n][3];
  NR14_REG = (uint8_t)(0x80u | kb_chime[n][4]);
}

void keel_boot(void) BANKED {
  static uint8_t row[32];
  uint8_t y, k, cut = 0, lcdc_bg = LCDC_REG & LCDCF_BG8000;
  DISPLAY_OFF;
  HIDE_SPRITES;
  HIDE_WIN;
  for (k = 0; k != 40u; k++) move_sprite(k, 0, 0);
  set_bkg_palette(0, 1, kb_palette);
  /* the map: palette 0 everywhere, cell 0 everywhere, the logo's 6x7 cells at the top rows */
  memset(row, 0, 32);
  VBK_REG = 1;
  for (y = 0; y != 32u; y++) set_bkg_tiles(0, y, 32, 1, row);
  VBK_REG = 0;
  for (y = 0; y != 32u; y++) {
    if (y < KB_ROWS)
      for (k = 0; k != KB_COLS; k++) row[KB_COL0 + k] = (uint8_t)(1u + y * KB_COLS + k);
    set_bkg_tiles(0, y, 32, 1, row);
    memset(row, 0, 32);
  }
  cells = (uint8_t *)(((uint16_t)cells_raw + 15u) & 0xfff0u);
  cut_cells(0);
  set_data((uint8_t *)0x8000u, cells, (KB_TILES + 1u) * 16u);
  LCDC_REG |= LCDCF_BG8000;
  move_bkg(0, (uint8_t)(-kb_y[0]));
  SHOW_BKG;
  DISPLAY_ON;
  frame_wait(); /* the enable frame (blank); frame 0 follows */
  for (k = 1; k != KB_FRAMES; k++) {
    if (k >= KB_FADE && kb_cut[k - KB_FADE] != cut) {
      cut = kb_cut[k - KB_FADE];
      cut_cells(cut);
      frame_wait();
      VBK_REG = 0;
      HDMA1_REG = (uint8_t)((uint16_t)cells >> 8);
      HDMA2_REG = (uint8_t)cells;
      HDMA3_REG = 0x00u; /* VRAM 0x8000 */
      HDMA4_REG = 0x00u;
      HDMA5_REG = (uint8_t)KB_TILES; /* general DMA, KB_TILES + 1 blocks */
    } else
      frame_wait();
    move_bkg(0, (uint8_t)(-(k < KB_LAND ? kb_y[k] : KB_LAND_Y)));
    if (k == KB_CHIME)
      chime_note(0);
    else if (k == KB_CHIME + kb_chime[0][0])
      chime_note(1);
    else if (k == KB_CHIME + kb_chime[0][0] + kb_chime[1][0])
      NR12_REG = 0;
  }
  /* The last frame is drawn; from the next one the game's frame shows: the title, which starts black. Every palette
   * goes black in this VBlank and the LCD stays on, so the title loads under a black screen (no blank LCD-off flash
   * between the dither and the title's fade-in). */
  frame_wait();
  memset(row, 0, 32);
  for (k = 0; k != 8u; k++) {
    set_bkg_palette(k, 1, (const uint16_t *)row);
    set_sprite_palette(k, 1, (const uint16_t *)row);
  }
  LCDC_REG = (uint8_t)((LCDC_REG & (uint8_t)~LCDCF_BG8000) | lcdc_bg);
  screen_dark = 1;
}
