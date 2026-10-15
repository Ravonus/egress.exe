/* The PRESS START title (docs/redesign.md 15): a fullscreen backrooms walk at 30 fps, the EGRESS.EXE logo glitching
 * in on its dark band, PRESS START blinking over the walk, and one info row cycling the clock, the SELECT prompt and
 * the seed.
 *
 * The walk is baked (tools/title3d/bake.py): TB_NLOOPS seeded loops through seeded mazes, each a closed corridor
 * cycle, streamed from ROM. Every title has its own seed (power-on noise of WRAM bank 7, DIV, the frame count and how
 * many titles came before); it picks the loop and the palette variant, and drives the light flicker and the logo's
 * glitches. Memory and timing: t3d.h. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include <string.h>
#include "egress.h"
#include "clock.h"
#include "title3d/t3d.h"
#include "title3d/gen/t3d_data.h"
#include "tb.h"
#ifdef T3D_TEST_HOLD
#include "mapper.h"
static uint16_t tst_vb, tst_nf;
#endif

uint16_t t3d_seed;
uint16_t t3d_seed_force;
uint8_t t3d_loop, t3d_var;
uint16_t t3d_frame;
uint8_t t3d_result;
volatile uint8_t t3d_freeze, t3d_frozen;
static uint8_t titles; /* titles shown since power-on */

#if TB_STATIC != T3D_STATIC || TB_JOB_MAX != T3D_JOB_MAX || TB_STAGE_TILES != T3D_STAGE_TILES || TB_MAX_JOBS != T3D_MAX_JOBS || \
    TB_MAX_RANGES != T3D_MAX_RANGES || TB_DICT_TILES != T3D_DICT_TILES
#error "the bake's slot layout and t3d.h disagree"
#endif

/* ---- seeded randomness (the flicker, the glitches) ---- */
static uint16_t rng;
static uint8_t rnd(void) {
  rng ^= rng << 7;
  rng ^= rng >> 9;
  rng ^= rng << 8;
  return (uint8_t)rng;
}

/* ---- palettes ---- */
static uint16_t mix555(uint16_t a, uint16_t b, uint8_t t) {
  uint8_t r = (uint8_t)(((a & 31u) * (16u - t) + (b & 31u) * t) >> 4);
  uint8_t g = (uint8_t)((((a >> 5) & 31u) * (16u - t) + ((b >> 5) & 31u) * t) >> 4);
  uint8_t bl = (uint8_t)((((a >> 10) & 31u) * (16u - t) + ((b >> 10) & 31u) * t) >> 4);
  return (uint16_t)(r | ((uint16_t)g << 5) | ((uint16_t)bl << 10));
}
#define BLACK 0u

static uint8_t level;      /* fade from black 0..16, everything */
static uint8_t vlevel;     /* the view, from the band's dark 0..16 (it comes up with the first frame) */
static uint8_t flick;      /* 0 lit .. 16 the dark palette */
static uint8_t logo_state; /* 0 off, 1 dim, 2 on, 3 flash, 4 chroma-swapped */

static uint16_t fade(uint16_t c) { return level >= 16u ? c : mix555(BLACK, c, level); }

static void pal_build(void) {
  uint8_t i;
  const uint16_t *lit = &T3D_VPAL[(uint16_t)t3d_var * (T3D_NBAND * 4u)];
  const uint16_t *dk = &T3D_VDARK[(uint16_t)t3d_var * (T3D_NBAND * 4u)];
  for (i = 0; i != T3D_NBAND * 4u; i++) {
    uint16_t c = flick ? mix555(lit[i], dk[i], flick) : lit[i];
    t3d_palx[i] = fade(vlevel >= 16u ? c : mix555(T3D_LOGO_PAL[0], c, vlevel));
  }
  for (i = 0; i != 4u; i++) {
    uint16_t c = T3D_LOGO_PAL[i];
    if (i) {
      if (logo_state == 0) c = T3D_LOGO_PAL[0];
      else if (logo_state == 1) c = mix555(T3D_LOGO_PAL[0], c, 7);
      else if (logo_state == 3) c = mix555(c, T3D_LOGO_PAL[3], 9);
      else if (logo_state == 4 && i < 3u) c = T3D_LOGO_PAL[3u - i];
    }
    t3d_palx[T3D_PAL_LOGO * 4u + i] = fade(c);
    t3d_palx[T3D_PAL_TEXT * 4u + i] = fade(T3D_TEXT_PAL[i]);
    t3d_palx[32u + i] = fade(T3D_SPR_PAL[i]);
  }
  t3d_pal_dirty = 1;
}

/* ---- VRAM (writes through set_data: GBDK waits for a free STAT mode per byte) ---- */
static void vset(uint16_t at, const uint8_t *src, uint16_t n) { set_data((uint8_t *)at, src, n); }

/* a BG tile id's address with BG tile data at 0x8800 (LCDC bit 4 clear): ids 0-127 at 0x9000, 128-255 at 0x8800 */
static uint16_t bg_addr(uint8_t id) { return id < 128u ? 0x9000u + (uint16_t)id * 16u : 0x8800u + (uint16_t)(id - 128u) * 16u; }

static uint8_t row[32], arow[32];

static void map_row(uint16_t base, uint8_t y) {
  VBK_REG = 0;
  vset(base + (uint16_t)y * 32u, row, 32);
  VBK_REG = 1;
  vset(base + (uint16_t)y * 32u, arow, 32);
  VBK_REG = 0;
}

static const char font_order[] = T3D_FONT_ORDER;
static uint8_t glyph(char c) {
  uint8_t i;
  for (i = 0; font_order[i]; i++)
    if (font_order[i] == c) return i;
  return 0;
}

static const uint8_t ZERO16[16];

static void video_init(void) {
  uint8_t x, y, k;
  uint16_t m;
  /* static tiles, bank 1: the blank, the logo, the font */
  VBK_REG = 1;
  vset(bg_addr(T3D_T_BLANK), ZERO16, 16);
  for (k = 0; k != T3D_LOGO_NT; k++) vset(bg_addr((uint8_t)(T3D_T_LOGO + k)), &T3D_LOGO_TILES[(uint16_t)k * 16u], 16);
  for (k = 0; k != T3D_FONT_N; k++) vset(bg_addr((uint8_t)(T3D_T_FONT + k)), &T3D_FONT2[(uint16_t)k * 16u], 16);
  VBK_REG = 0;
  vset(bg_addr(T3D_T_BLANK), ZERO16, 16); /* bank 0's id 152: the view's blank before the first frame */
  /* PRESS START's letters: OBJ tiles 0-5 at 0x8000 (bank 0) */
  set_sprite_data(0, sizeof T3D_SPR_TILES / 16u, T3D_SPR_TILES);
  /* both BG maps: the band (blank row, the logo), the view (blank until the first frame), the row under the window */
  for (m = 0x9800u; m != 0xA000u; m += 0x400u) {
    for (y = T3D_ROW_LOGO; y != 32u; y++) {
      for (x = 0; x != 32u; x++) {
        if (y > T3D_ROW_LOGO && y < T3D_ROW_VIEW && x < 20u) {
          row[x] = (uint8_t)(T3D_T_LOGO + T3D_LOGO_MAP[(uint16_t)(y - T3D_ROW_LOGO - 1u) * 20u + x]);
          arow[x] = 0x08u | T3D_PAL_LOGO;
        } else if (y >= T3D_ROW_VIEW && y < T3D_ROW_VIEW + TB_TH) {
          row[x] = T3D_T_BLANK; /* bank 0's id 152, cleared above: the stream reaches it only after the keyframe */
          arow[x] = T3D_BAND[y - T3D_ROW_VIEW]; /* the view's tiles are all in VRAM bank 0: these never change */
        } else {
          row[x] = T3D_T_BLANK;
          arow[x] = (uint8_t)(0x08u | (y < T3D_ROW_VIEW ? T3D_PAL_LOGO : T3D_PAL_TEXT));
        }
      }
      map_row(m, y);
    }
  }
  /* the window: 0x9C00 row 0 (outside the BG's rows 14-31), the font in bank 1, the text palette */
  for (x = 0; x != 32u; x++) {
    row[x] = T3D_T_FONT; /* a space */
    arow[x] = 0x08u | T3D_PAL_TEXT;
  }
  map_row(0x9C00u, 0);
  /* the shadow map: the blank */
  memset(T3D_SHADOW, T3D_T_BLANK, (uint16_t)TB_TH * 32u);
  SCX_REG = 0;
  SCY_REG = T3D_SCY;
  WY_REG = T3D_WY;
  WX_REG = 7u;
}

/* the window's text (row 0 of map 0x9C00) */
static void text(uint8_t x, const char *s) {
  uint8_t n = 0;
  while (s[n] && x + n < 20u) {
    row[n] = (uint8_t)(T3D_T_FONT + glyph(s[n]));
    n++;
  }
  VBK_REG = 0;
  vset(0x9C00u + x, row, n);
}

/* ---- PRESS START: ten sprites over the walk ---- */
static const char PRESS[] = "PRESS START";
static uint8_t press_on = 2;
static void press_show(uint8_t on) {
  uint8_t i, k = 0;
  const char *order = T3D_SPR_TEXT;
  if (on == press_on) return;
  press_on = on;
  for (i = 0; PRESS[i]; i++) {
    uint8_t t;
    if (PRESS[i] == ' ') continue;
    for (t = 0; order[t] != PRESS[i]; t++) {
    }
    set_sprite_tile(k, t);
    set_sprite_prop(k, 0);
    move_sprite(k, on ? (uint8_t)(36u + i * 8u + 8u) : 0u, on ? (uint8_t)(T3D_SPR_Y + 16u) : 0u);
    k++;
  }
}

/* ---- the info row: cycles every ~2 s between the date and time (or SELECT: SET CLOCK while the clock was never
 * set), the SELECT prompt (or the drift prompt's two lines, which main's rule shows only from the 2nd power-on since a
 * set), and the seed ---- */
static const char HEX[] = "0123456789ABCDEF";
static uint8_t info_slot, info_tick;
static void info_row(const char *s) {
  static char r[21];
  uint8_t n = (uint8_t)strlen(s), x0 = (uint8_t)((20u - n) >> 1), i;
  memset(r, ' ', 20);
  r[20] = 0;
  for (i = 0; i != n; i++) r[x0 + i] = s[i];
  text(0, r);
}
static void seed_line(void) {
  static char s[17];
  memcpy(s, "SEED 0000 * KEEL", 17);
  s[5] = HEX[t3d_seed >> 12];
  s[6] = HEX[(t3d_seed >> 8) & 15u];
  s[7] = HEX[(t3d_seed >> 4) & 15u];
  s[8] = HEX[t3d_seed & 15u];
  info_row(s);
}
static uint8_t info_slots(void) { return (uint8_t)(!clk_known ? 2u : clk_drift ? 4u : 3u); }
static void clock_line(uint8_t slot) {
  static char s[19];
  uint8_t last = (uint8_t)(info_slots() - 1u);
  if (slot >= last) {
    seed_line();
    return;
  }
  if (slot == 0u) {
    clock_title_text(0, s); /* the date and time, or SELECT: SET CLOCK */
    info_row(s[0] == ' ' ? s + 1 : s);
  } else if (clk_drift) {
    clock_title_text(slot, s); /* 1: MAY HAVE DRIFTED, 2: SELECT TO SET IT */
    info_row(s + 1);
  } else {
    info_row("SELECT: SET CLOCK");
  }
}

/* ---- entropy for the seed ---- */
static uint16_t entropy(void) {
  /* WRAM bank 7 is never written by the game: on hardware it holds the power-on noise of the RAM cells */
  uint16_t h = 0x1D3Bu;
  const uint8_t *p = (const uint8_t *)0xD000u;
  uint16_t i;
  SVBK_REG = 7;
  for (i = 0; i < 0x1000u; i += 7u) h = (uint16_t)((h << 5) + (h >> 11) + p[i]);
  SVBK_REG = T3D_WBANK;
  return h;
}

/* ---- the logo's neon: ignition flicker, then rare glitch bursts ---- */
static const uint8_t IGNITE[] = {0, 0, 0, 1, 0, 0, 2, 0, 1, 0, 3, 2, 0, 2, 2, 4, 2, 0, 1, 2, 2, 2, 3, 2};
static uint16_t logo_t, next_glitch, next_flick;
static uint8_t glitch_left, flick_left;

static void logo_update(uint8_t dt) {
  uint8_t st = logo_state, i;
  logo_t += dt;
  if (logo_t < 30u) {
    st = 0;
  } else if (logo_t < 30u + sizeof IGNITE * 4u) {
    st = IGNITE[(logo_t - 30u) >> 2];
    t3d_glitch = (uint8_t)(st != 2u);
    for (i = 0; i != 4u; i++) t3d_scx[i] = t3d_glitch ? (uint8_t)((rnd() & 7u) - 3u) : 0u;
  } else if (glitch_left) {
    glitch_left = glitch_left > dt ? (uint8_t)(glitch_left - dt) : 0u;
    st = (rnd() & 3u) == 0 ? 4u : (rnd() & 7u) == 0 ? 3u : 2u;
    t3d_glitch = 1;
    for (i = 0; i != 4u; i++) t3d_scx[i] = (uint8_t)((rnd() & 3u) ? 0u : (rnd() & 7u) - 3u);
    if (!glitch_left) t3d_glitch = 0;
  } else {
    st = 2;
    t3d_glitch = 0;
    if (logo_t >= next_glitch) {
      glitch_left = (uint8_t)(6u + (rnd() & 7u));
      next_glitch = (uint16_t)(logo_t + 180u + rnd() + rnd());
    }
  }
  if (st != logo_state) {
    logo_state = st;
    pal_build();
  }
}

/* the fluorescent tubes: a stutter of dark frames every few seconds, with the buzz */
static const uint8_t FLICK[] = {11, 0, 12, 12, 0, 0, 9, 0};
static void flicker_update(uint8_t dt) {
  uint8_t f = 0;
  if (flick_left) {
    flick_left = flick_left > dt ? (uint8_t)(flick_left - dt) : 0u;
    f = FLICK[(sizeof FLICK - 1u) - ((flick_left >> 1) & 7u)];
  } else if (logo_t >= next_flick) {
    flick_left = 16u;
    next_flick = (uint16_t)(logo_t + 150u + rnd() + (rnd() >> 1));
    t3d_buzz = 1;
  }
  if (f != flick) {
    flick = f;
    pal_build();
  }
}

static void sound_call(uint8_t what) {
  t3d_sound = 0;
  if (what == 0) {
    music_play(SONG_LOBBY);
    music_fade(0);
  } else if (what == 1) {
    sfx(SFX_EXIT);
    music_fade(1);
  } else {
    sfx(SFX_STEP);
  }
  t3d_sound = 1;
}

/* ---- the stream player ---- */
/* harness profile: scanlines (108.7 us: 228 M-cycles in double speed) spent per segment, summed: 0 playing a record
 * (the map runs into the shadow), 1 waiting for the last frame to flip in, 2 the rest of the loop */
uint32_t t3d_prof[3];
static uint16_t prof_t;
static void prof(uint8_t k) {
  uint16_t v, t;
  uint8_t ly;
  do {
    v = t3d_vbl;
    ly = LY_REG;
  } while (v != t3d_vbl);
  t = (uint16_t)(v * 154u + (ly >= 144u ? ly - 144u : ly + 10u)); /* from the VBlank (LY reads 0 on line 153) */
  t3d_prof[k] += (uint16_t)(t - prof_t);
  prof_t = t;
}

/* frame f's record (f 0 is the keyframe the first time, the wrap record W after; then frame 1's follows) */
static void play_frame(uint16_t f) {
  prof(2);
  tb_play();
  if (!f) tb_frame1(t3d_loop);
  prof(0);
}

void title3d_main(void) BANKED {
  uint8_t lcd_was_on, lcdc0, i, dt, out = 0;
  uint16_t lastv, v, out_t = 0, clock_t, nfr, f = 0;

  SVBK_REG = T3D_WBANK;
  if (!titles) t3d_seed ^= entropy();
  if (t3d_seed_force) t3d_seed = t3d_seed_force;
#ifdef T3D_TEST_SEED
  t3d_seed = T3D_TEST_SEED;
#endif
  if (!t3d_seed) t3d_seed = 0x5EEDu;
  titles++;
  rng = t3d_seed;
  t3d_loop = (uint8_t)(t3d_seed % TB_NLOOPS);
  t3d_var = (uint8_t)((t3d_seed >> 4) % T3D_NVAR);

  /* video under black palettes (the LCD may be on: after the KEEL boot it stays on, black) */
  level = 0;
  vlevel = 0;
  flick = 0;
  logo_state = 1; /* the tube is there, unlit, until the walk starts */
  logo_t = 0;
  glitch_left = flick_left = 0;
  next_glitch = 400u;
  next_flick = (uint16_t)(90u + (rnd() & 63u));
  t3d_glitch = 0;
  t3d_map_ready = 0;
  t3d_freeze = t3d_frozen = 0;
  t3d_nflips = 0;
  t3d_cyc_tiles = t3d_cyc_pal = 0;
  t3d_flip_late = t3d_flip_line_max = 0;
  press_on = 2;
  pal_build();
  lcd_was_on = (LCDC_REG & LCDCF_ON) ? 1u : 0u;
  lcdc0 = LCDC_REG;
  if (lcd_was_on) {
    wait_vbl_done();
    BCPS_REG = 0x80u;
    for (i = 0; i != 64u; i++) BCPD_REG = 0;
    OCPS_REG = 0x80u;
    for (i = 0; i != 64u; i++) OCPD_REG = 0;
  }
  press_show(0);
  LCDC_REG = (uint8_t)((LCDC_REG | LCDCF_BGON | LCDCF_OBJON | LCDCF_WINON | LCDCF_WIN9C00) &
                       (uint8_t)~(LCDCF_BG8000 | LCDCF_BG9C00 | LCDCF_OBJ16));
  video_init();
  info_slot = info_tick = 0;
  clock_line(0);
  t3d_vbl = 0;
  t3d_isr_on();
  if (!lcd_was_on) DISPLAY_ON;
  sound_call(0);
  /* the band, the logo's dark tube and the text fade in; the loop's first frame (its keyframe) streams in */
  nfr = tb_frames(t3d_loop);
  tb_open(t3d_loop);
  play_frame(0);
  while (level < 16u) {
    wait_vbl_done();
    level = (uint8_t)(level + 2u);
    pal_build();
  }

  lastv = t3d_vbl;
  clock_t = 0;
  t3d_result = 0;
  while (1) {
    /* the last frame is on screen once its map has flipped in */
    prof(2);
    while (t3d_map_ready) {
    }
    prof(1);
    t3d_frame = f;
#ifdef T3D_TEST_HOLD
    if (t3d_nflips == 200u) { /* the steady rate is measured from here (the fade and the keyframe are behind) */
      tst_vb = t3d_vbl;
      tst_nf = t3d_nflips;
    }
    /* a SameBoy test build (tools/title3d/sameboy_check.py): at this frame, the measurements into cartridge RAM (the
     * tester saves it), then the walk holds still for the screenshot */
    if (f == T3D_TEST_HOLD && t3d_nflips > nfr) { /* the second time round: past the wrap */
      volatile uint8_t *m = (volatile uint8_t *)0xBF00u;
      uint16_t vb = t3d_vbl, nf = t3d_nflips;
      flick = 0; /* the lights steady, so the screenshots compare */
      pal_build();
      while (t3d_pal_dirty) {
      }
      EG_SRAM_OPEN();
      m[0] = 'T';
      m[1] = '3';
      m[2] = t3d_loop;
      m[3] = (uint8_t)f;
      m[4] = (uint8_t)(f >> 8);
      m[5] = (uint8_t)vb;
      m[6] = (uint8_t)(vb >> 8);
      m[7] = (uint8_t)nf;
      m[8] = (uint8_t)(nf >> 8);
      m[9] = t3d_flip_late;
      m[10] = t3d_flip_line_max;
      m[11] = t3d_var;
      m[12] = (uint8_t)(vb - tst_vb);
      m[13] = (uint8_t)((uint16_t)(vb - tst_vb) >> 8);
      m[14] = (uint8_t)(nf - tst_nf);
      m[15] = (uint8_t)((uint16_t)(nf - tst_nf) >> 8);
      EG_SRAM_CLOSE();
      while (1) {
      }
    }
#endif
    if (t3d_freeze) {
      t3d_frozen = 1;
      while (t3d_freeze) {
      }
      t3d_frozen = 0;
    }
    /* the next frame first, so its uploads make the next VBlank (the loop wraps to its keyframe) */
    if (++f == nfr) f = 0;
    play_frame(f);
    v = t3d_vbl;
    dt = (uint8_t)((uint16_t)(v - lastv) > 8u ? 8u : (v - lastv));
    lastv = v;
    logo_update(dt);
    flicker_update(dt);
    if (vlevel < 16u && !out) {
      vlevel = (uint8_t)(vlevel + dt > 16u ? 16u : vlevel + dt);
      pal_build();
    }
    press_show((uint8_t)(!out && vlevel >= 16u && !((v >> 5) & 1u)));
    /* the info row, about once a second: the clock ticks on, every 2nd the row moves on */
    clock_t += dt;
    if (clock_t >= 64u && !out) {
      clock_t = 0;
      if (++info_tick & 1u) {
        if (++info_slot >= info_slots()) info_slot = 0;
      }
      clock_line(info_slot);
    }
    /* START (or A) plays; SELECT opens SETUP; both fade out over the walk */
    if (!out) {
      uint8_t j = vlevel >= 16u ? t3d_keys : 0u; /* a press while the walk fades in waits for it */
      if (j) t3d_keys = 0;
      if (j & (J_START | J_A)) {
        out = T3D_START;
        sound_call(1);
      } else if (j & J_SELECT) {
        out = T3D_SELECT;
        sound_call(2);
      }
      out_t = v;
    } else {
      uint16_t e = (uint16_t)(v - out_t);
      level = e >= 32u ? 0u : (uint8_t)(16u - (e >> 1));
      pal_build();
      if (e >= 34u) break;
    }
  }
  /* leave: the last palettes black, sprites away, the game's video and interrupts back, WRAM bank 1 back */
  while (t3d_map_ready) {
  }
  wait_vbl_done();
  wait_vbl_done();
  t3d_sound = 0;
  t3d_isr_off();
  press_show(0);
  BCPS_REG = 0x80u;
  for (i = 0; i != 64u; i++) BCPD_REG = 0;
  LCDC_REG = (uint8_t)((LCDC_REG & (uint8_t)~(LCDCF_BG8000 | LCDCF_BG9C00 | LCDCF_WINON | LCDCF_WIN9C00 | LCDCF_OBJON)) |
                       (lcdc0 & (uint8_t)(LCDCF_BG8000 | LCDCF_BG9C00 | LCDCF_WINON | LCDCF_WIN9C00 | LCDCF_OBJON)));
  SCY_REG = 0;
  WY_REG = 144u;
  VBK_REG = 0;
  SVBK_REG = 1;
  t3d_result = out;
}

uint8_t title3d_run(void) BANKED {
  memset(t3d_stack, 0xA5, T3D_STACK); /* the harness reads the high-water mark */
  t3d_seed = (uint16_t)(t3d_seed * 0x6255u + 0x3619u) ^ DIV_REG ^ (frame_count << 5) ^ titles;
  t3d_on_stack();
  return t3d_result;
}
