/* Trick test ROM for the fullscreen renderer's display tricks (proto/raycast/v2/src/lcd2.s, assembled with TT = 1).
   It runs the real ISR on a fixed test image, measures each trick and shows the result:

     view (lines 0-127): an ellipse, a diagonal and a 4x4 checker, drawn in 64 framebuffer rows that only look
       right (round, straight, square) when every row is shown on exactly two lines
     bottom of the view: five result boxes, green = pass, red = fail, and under each the measured number
       1 ROW2X   SCY written in mode 2 on every doubled line   (number: SCY writes seen outside mode 2)
       2 HUDPAL  HUD palette written outside mode 3 at line 127 (number: writes that touched mode 3)
       3 GDMA    112-block GDMA done inside VBlank             (number: spare 16-cycle timer ticks after it)
       4 VBLEND  copy + flip + stats done before line 0        (number: spare ticks of VBlank)
       5 WRAMBK  WRAM banks 2 / 3 hold separate framebuffers, flips at 1 per 2 frames (number: flips / 2 s)
     HUD strip (lines 128-143): must be the HUD's colours (grey rule, dark row), never the magenta that palette 0
       holds above line 127; the top-left 8x4 corner flips between colour 1 (buffer A) and 2 (buffer B).

   Results also go to cartridge RAM (for tt_check.py on a SameBoy run): "TT", then the five numbers and flags. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include <stdint.h>

#define TW 20u
#define TH 8u
#define VIEW_H 64u
#define HUDT 224u

#include <gb/isr.h>
extern void lyc_isr(void);
ISR_VECTOR(VECTOR_STAT, lyc_isr)

/* lcd2.s shares these with main2.c */
volatile uint16_t vbl;
volatile uint8_t pend, pend_buf, front, pal_dirty, hud_dirty;
uint8_t hud_t[2];
uint16_t pal_cur[32];
uint8_t hud_pal[8];
uint16_t log_flip[64], pend_view, nflips, copy_lines, copy_calls;
uint8_t vbl_end_max;
extern uint8_t q_len, q_off;
/* TT hooks in lcd2.s */
uint8_t tt_scy_bad, tt_pal_bad, tt_dma_t, tt_end_t, tt_scy_mode, tt_scy_ly;

static const uint16_t VIEWPAL[4] = {RGB8(16, 24, 40), RGB8(40, 160, 70), RGB8(200, 40, 40), RGB8(240, 240, 220)};
static const uint16_t MAGENTA[4] = {RGB8(255, 0, 255), RGB8(255, 0, 255), RGB8(255, 0, 255), RGB8(255, 0, 255)};
static const uint16_t HUDPAL[4] = {RGB8(10, 10, 14), RGB8(90, 90, 100), RGB8(160, 160, 170), RGB8(230, 230, 230)};

static const uint8_t DIG[10][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 3, 1, 7}, {5, 5, 7, 1, 1},
                                   {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 2, 2}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}};

/* framebuffer pixel: tile column x >> 3 is a 128-byte strip, row r at 2 r */
static void px(uint8_t *fb, uint8_t x, uint8_t r, uint8_t c) {
  uint8_t *p = fb + (uint16_t)(x >> 3) * 128u + (uint16_t)r * 2u;
  uint8_t m = (uint8_t)(0x80u >> (x & 7u));
  p[0] = (uint8_t)((c & 1u) ? (p[0] | m) : (p[0] & (uint8_t)~m));
  p[1] = (uint8_t)((c & 2u) ? (p[1] | m) : (p[1] & (uint8_t)~m));
}

static void boxes(uint8_t *fb, uint8_t k, uint8_t ok, uint8_t v) {
  uint8_t x0 = (uint8_t)(6u + k * 32u), x, r, d, i;
  for (r = 50; r < 56u; r++)
    for (x = x0; x < (uint8_t)(x0 + 20u); x++) px(fb, x, r, ok ? 1u : 2u);
  /* the number, 3 digits of 3x5 under the box */
  for (i = 0; i < 3u; i++) {
    d = i == 0 ? v / 100u : i == 1 ? (v / 10u) % 10u : v % 10u;
    for (r = 0; r < 5u; r++)
      for (x = 0; x < 3u; x++) px(fb, (uint8_t)(x0 + 4u + i * 4u + x), (uint8_t)(57u + r), (DIG[d][r] >> (2u - x)) & 1u ? 3u : 0u);
  }
}

static void image(uint8_t *fb, uint8_t which) {
  int16_t x, r;
  memset(fb, 0, TW * TH * 16u);
  /* ellipse rx 40 px, ry 20 rows (a circle of radius 40 on screen) around (80, 24) */
  for (r = 0; r < 48; r++)
    for (x = 0; x < 160; x++) {
      int16_t dx = x - 80, dy = (int16_t)(r - 24) * 2;
      int16_t q = (int16_t)(dx * dx + dy * dy);
      if (q <= 1600 && q > 1450) px(fb, (uint8_t)x, (uint8_t)r, 3u);
    }
  /* diagonal: one px right per screen line (two per framebuffer row) */
  for (r = 0; r < 48; r++) {
    px(fb, (uint8_t)(r * 2), (uint8_t)r, 3u);
    px(fb, (uint8_t)(r * 2 + 1), (uint8_t)r, 3u);
  }
  /* 4x4-on-screen checker at the right: 4 px by 2 rows */
  for (r = 0; r < 24; r++)
    for (x = 128; x < 160; x++)
      if ((((x - 128) >> 2) + (r >> 1)) & 1) px(fb, (uint8_t)x, (uint8_t)(r + 24), 1u);
  /* flip indicator */
  for (r = 0; r < 4; r++)
    for (x = 0; x < 8; x++) px(fb, (uint8_t)x, (uint8_t)r, which ? 2u : 1u);
}

static uint8_t wram_ok(void) {
  uint8_t ok = 1;
  SVBK_REG = 2;
  *(volatile uint8_t *)0xDFF0u = 0xA2u;
  SVBK_REG = 3;
  *(volatile uint8_t *)0xDFF0u = 0xB3u;
  SVBK_REG = 2;
  if (*(volatile uint8_t *)0xDFF0u != 0xA2u) ok = 0;
  SVBK_REG = 3;
  if (*(volatile uint8_t *)0xDFF0u != 0xB3u) ok = 0;
  SVBK_REG = 1;
  return ok;
}

void main(void) {
  uint8_t x, y, b, k, cur = 0, wok;
  uint16_t v0, f0;
  uint8_t ok[5], val[5];
  volatile uint8_t *sram = (volatile uint8_t *)0xA000u;
  cpu_fast();
  DISPLAY_OFF;
  ENABLE_RAM;
  for (x = 0; x < 4u; x++) {
    pal_cur[x] = MAGENTA[x];
    pal_cur[4u + x] = VIEWPAL[x];
  }
  for (x = 0; x < 4u; x++) {
    hud_pal[2u * x] = (uint8_t)HUDPAL[x];
    hud_pal[2u * x + 1u] = (uint8_t)(HUDPAL[x] >> 8);
  }
  set_bkg_palette(0, 2, pal_cur);
  for (b = 0; b < 2u; b++) {
    VBK_REG = b;
    memset((void *)0x8000u, 0, 256u * 16u);
  }
  VBK_REG = 0;
  /* HUD tiles: 224 = solid colour 1 rule, 225 = solid colour 0 */
  memset((void *)(0x8000u + HUDT * 16u), 0, 32u);
  for (x = 0; x < 8u; x++) *(uint8_t *)(0x8000u + HUDT * 16u + 2u * x) = 0xFFu;
  /* maps: rows 0-7 view tiles x * 8 + y in palette 1 (bank 0 at 9800, bank 1 at 9C00), rows 8-9 HUD in palette 0 */
  for (y = 0; y < 32u; y++)
    for (x = 0; x < 32u; x++) {
      uint8_t t = 0, a = 1;
      if (y < TH && x < TW) t = (uint8_t)(x * 8u + y);
      else if (y == TH) { t = HUDT; a = 0; }
      else { t = HUDT + 1u; a = 0; }
      VBK_REG = 0;
      *(uint8_t *)(0x9800u + y * 32u + x) = t;
      *(uint8_t *)(0x9C00u + y * 32u + x) = t;
      VBK_REG = 1;
      *(uint8_t *)(0x9800u + y * 32u + x) = a;
      *(uint8_t *)(0x9C00u + y * 32u + x) = (uint8_t)(y < TH ? (a | 0x08u) : a);
    }
  VBK_REG = 0;
  wok = wram_ok();
  SVBK_REG = 2;
  image((uint8_t *)0xD000u, 0);
  SVBK_REG = 3;
  image((uint8_t *)0xD000u, 1);
  SVBK_REG = 1;
  TAC_REG = 0x06u; /* 65536 Hz, doubled: one tick = 16 double-speed cycles; lcd2.s zeroes TIMA at line 144 */
  LCDC_REG = (uint8_t)((LCDC_REG | LCDCF_BG8000 | LCDCF_BGON) & (uint8_t)~LCDCF_BG9C00);
  front = 0;
  disable_interrupts();
  LYC_REG = 1u;
  STAT_REG = STATF_LYC;
  set_interrupts(LCD_IFLAG);
  enable_interrupts();
  DISPLAY_ON;
  v0 = vbl;
  f0 = nflips;
  for (;;) {
    /* queue the other buffer as soon as the last copy flipped: 160 blocks = one full VBUDGET VBlank + one */
    while (pend) {
    }
    cur ^= 1u;
    pend_buf = cur;
    q_len = (uint8_t)(TW * TH);
    q_off = 0;
    pend_view++;
    pend = 1;
    if ((uint16_t)(vbl - v0) >= 120u) {
      uint8_t fl = (uint8_t)(nflips - f0);
      ok[0] = tt_scy_bad == 0;
      val[0] = tt_scy_bad;
      ok[1] = tt_pal_bad == 0;
      val[1] = tt_pal_bad;
      /* VBlank is 10 lines = 142 ticks; the GDMA ends at tick tt_dma_t, everything at tt_end_t */
      ok[2] = tt_dma_t < 142u;
      val[2] = (uint8_t)(tt_dma_t < 142u ? 142u - tt_dma_t : 0u);
      ok[3] = tt_end_t < 142u && vbl_end_max <= 153u;
      val[3] = (uint8_t)(tt_end_t < 142u ? 142u - tt_end_t : 0u);
      ok[4] = wok && fl == 60u;
      val[4] = fl;
      while (pend) {
      }
      for (b = 0; b < 2u; b++) {
        SVBK_REG = (uint8_t)(2u + b);
        for (k = 0; k < 5u; k++) boxes((uint8_t *)0xD000u, k, ok[k], val[k]);
      }
      SVBK_REG = 1;
      sram[0] = 'T';
      sram[1] = 'T';
      for (k = 0; k < 5u; k++) {
        sram[2u + k] = ok[k];
        sram[8u + k] = val[k];
      }
      sram[14] = vbl_end_max;
      sram[15] = tt_dma_t;
      sram[16] = tt_end_t;
      sram[17] = tt_scy_mode;
      sram[18] = tt_scy_ly;
      v0 = vbl;
      f0 = nflips;
    }
  }
}
