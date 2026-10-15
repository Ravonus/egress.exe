/* The PRESS START title (docs/redesign.md 15): a slow, seeded backrooms walk in the v2 raycaster, the EGRESS.EXE logo
 * glitching in above it, PRESS START blinking below it, the clock row and the walk's seed.
 *
 * Every title has its own seed (power-on noise of WRAM bank 7, DIV and the frame count, plus how many titles came
 * before), so the maze, its dressing (posters, outlets, CRTs, stains, doors, lights-out patches), the walker's turns,
 * the light flicker and the logo's glitches differ every boot and every return. The maze, the walker and the dressing
 * are the prototype's (proto/raycast/v2/src/main2.c, scene 4), seeded. Memory and timing: t3d.h. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include <string.h>
#include "egress.h"
#include "clock.h"
#include "title3d/t3d.h"
#include "title3d/gen/t3d_tex.h"
#include "title3d/gen/t3d_data.h"

uint16_t t3d_seed;
uint16_t t3d_seed_force;
uint8_t t3d_result;
volatile uint8_t t3d_freeze, t3d_frozen;
uint8_t t3d_last_bank; /* the bank of the last rendered view (harness) */
static uint8_t titles;  /* titles shown since power-on */

#define map T3D_MAP
#define MAPW T3D_MAPW

/* ---- seeded randomness ---- */
static uint16_t rng;
static uint8_t rnd(void) {
  rng ^= rng << 7;
  rng ^= rng >> 9;
  rng ^= rng << 8;
  return (uint8_t)rng;
}

/* 0..n-1 for n <= 4, and 1..29, by rejection (no division: the generator runs while the screen is black) */
static uint8_t rnd_n(uint8_t n) {
  uint8_t r;
  do r = (uint8_t)(rnd() & 3u);
  while (r >= n);
  return r;
}
static uint8_t rnd_29(void) {
  uint8_t r;
  do r = (uint8_t)(rnd() & 31u);
  while (r >= 29u);
  return (uint8_t)(r + 1u);
}

/* ---- the maze (v1's generator, seeded): 31x31 cells, loops, doors at dead ends ---- */
static const int8_t DX[4] = {1, 0, -1, 0};
static const int8_t DY[4] = {0, 1, 0, -1};

static uint8_t at(uint8_t x, uint8_t y) {
  uint8_t c = map[(uint16_t)y * MAPW + x];
  return c < 0x80u ? c : 0u;
}
static uint8_t is_dead(uint8_t x, uint8_t y) {
  uint16_t i = (uint16_t)y * MAPW + x;
  return T3D_DEAD[i >> 3] & (uint8_t)(1u << (i & 7u));
}
static void set_dead(uint8_t x, uint8_t y) {
  uint16_t i = (uint16_t)y * MAPW + x;
  T3D_DEAD[i >> 3] |= (uint8_t)(1u << (i & 7u));
}

static void gen_maze(void) {
  uint8_t *sx = T3D_STK, *sy = T3D_STK + 0x100u; /* the carve stack: cell x, y */
  uint16_t i;
  uint8_t sp = 0, x, y, n, k, d, dirs[4];
  for (i = 0; i < MAPW * MAPW; i++) map[i] = 1;
  memset(T3D_DEAD, 0, 128);
  map[MAPW + 1] = 0;
  sx[0] = 1;
  sy[0] = 1;
  sp = 1;
  while (sp) {
    x = sx[sp - 1u];
    y = sy[sp - 1u];
    n = 0;
    for (d = 0; d < 4; d++) {
      int8_t nx = (int8_t)(x + DX[d] * 2), ny = (int8_t)(y + DY[d] * 2);
      if (nx < 1 || ny < 1 || nx > 29 || ny > 29) continue;
      if (at((uint8_t)nx, (uint8_t)ny)) dirs[n++] = d;
    }
    if (!n) {
      sp--;
      continue;
    }
    d = dirs[rnd_n(n)];
    map[(uint16_t)(y + DY[d]) * MAPW + (uint8_t)(x + DX[d])] = 0;
    x = (uint8_t)(x + DX[d] * 2);
    y = (uint8_t)(y + DY[d] * 2);
    map[(uint16_t)y * MAPW + x] = 0;
    sx[sp] = x;
    sy[sp] = y;
    sp++;
  }
  /* loops: knock through some walls between two open cells */
  for (k = 0; k < 28u; k++) {
    x = rnd_29();
    y = rnd_29();
    if (((x ^ y) & 1u) == 0) continue;
    if ((x & 1u) == 0 && !at(x - 1u, y) && !at(x + 1u, y)) map[(uint16_t)y * MAPW + x] = 0;
    if ((y & 1u) == 0 && !at(x, y - 1u) && !at(x, y + 1u)) map[(uint16_t)y * MAPW + x] = 0;
  }
  /* dead-end corridors (the walker keeps out of them): prune leaves until only the loops and the corridors between
   * them are left. deg (open neighbours) lives in the framebuffer, the leaf stack in the scratch; both are free
   * until the first view. */
  {
    uint8_t *deg = (uint8_t *)T3D_FB;
    uint16_t *leaf = (uint16_t *)T3D_STK, c;
    uint16_t ln = 0;
    memset(deg, 0, MAPW * MAPW);
    for (y = 1; y < 30u; y++)
      for (x = 1; x < 30u; x++) {
        if (at(x, y)) continue;
        c = (uint16_t)y * MAPW + x;
        n = (uint8_t)(!at((uint8_t)(x + 1u), y) + !at((uint8_t)(x - 1u), y) + !at(x, (uint8_t)(y + 1u)) +
                      !at(x, (uint8_t)(y - 1u)));
        deg[c] = n;
        if (n <= 1u) leaf[ln++] = c;
      }
    while (ln) {
      c = leaf[--ln];
      x = (uint8_t)(c & 31u);
      y = (uint8_t)(c >> 5);
      if (is_dead(x, y)) continue;
      set_dead(x, y);
      for (d = 0; d < 4; d++) {
        uint8_t nx = (uint8_t)(x + DX[d]), ny = (uint8_t)(y + DY[d]);
        uint16_t nc = (uint16_t)ny * MAPW + nx;
        if (at(nx, ny) || is_dead(nx, ny)) continue;
        if (--deg[nc] == 1u) leaf[ln++] = nc;
      }
    }
  }
  /* doors: most dead ends end in one, and a few stand in the live corridors' walls */
  for (y = 1; y < 30u; y++)
    for (x = 1; x < 30u; x++) {
      uint8_t open = 0, od = 0, live;
      if (at(x, y)) continue;
      for (d = 0; d < 4; d++)
        if (!at((uint8_t)(x + DX[d]), (uint8_t)(y + DY[d]))) {
          open++;
          od = d;
        }
      if (open == 1u && (rnd() & 3u)) {
        d = (od + 2u) & 3u;
        map[(uint16_t)(y + DY[d]) * MAPW + (uint8_t)(x + DX[d])] = 2;
      }
      live = !is_dead(x, y);
      if (live && open == 2u && (rnd() & 7u) == 0) {
        d = (uint8_t)(rnd() & 3u);
        if (at((uint8_t)(x + DX[d]), (uint8_t)(y + DY[d])) == 1u)
          map[(uint16_t)(y + DY[d]) * MAPW + (uint8_t)(x + DX[d])] = 2;
      }
    }
}

/* ---- dressing: texture ids and floor sets from a seeded hash (8x8 zones keep the wallpaper coherent, 4x4 patches
 * have their lights out) ---- */
static const uint8_t O_DECOR[16] = {O_OUTLET, O_OUTLET, O_POSTER, O_VENT,  O_CRT,    O_STAIN, O_WATER, O_CLOCK,
                                    O_EXIT,   O_POSTER, O_STAIN,  O_OUTLET, O_POSTER, O_CRT,   O_VENT,  O_CLOCK};

static void dress(void) {
  uint8_t *zone = T3D_STK + 0x200u, *dark = T3D_STK + 0x210u; /* 4x4 zones of 8x8 cells, 8x8 patches of 4x4 */
  uint8_t x, y, k;
  for (k = 0; k != 16u; k++) zone[k] = (uint8_t)(rnd() % 3u);
  for (k = 0; k != 64u; k++) dark[k] = (uint8_t)((rnd() & 7u) == 0u);
  for (y = 0; y < MAPW; y++)
    for (x = 0; x < MAPW; x++) {
      uint16_t i = (uint16_t)y * MAPW + x;
      uint8_t c = map[i], h = rnd();
      if (!c) {
        map[i] = (uint8_t)(0x80u | (dark[((y >> 2) << 3) | (x >> 2)] ? S_OFFDARK : S_OFFICE));
      } else if (c == 2) {
        map[i] = (h & 7u) == 0 ? O_DOOREXIT : O_DOOR;
      } else if (h < 4u) {
        map[i] = O_TEAR;
      } else if (h < 92u) {
        map[i] = O_DECOR[h & 15u];
      } else {
        map[i] = (uint8_t)(O_BASE0 + zone[((y >> 3) << 2) | (x >> 3)]);
      }
    }
}

/* ---- the walker: a seeded random walk through the live corridors, turning on the spot, the camera trailing ---- */
#define MOVE_SPD 4u
#define TURN_SPD 6u
#define AHEAD 4u
#define TRAIL 96u
static uint8_t path_x[32], path_y[32];
static uint8_t sim_x, sim_y, sim_d, path_n, wk, wfrac;

static void path_push(void) {
  uint8_t d, opts[3], n = 0, k;
  path_x[path_n & 31u] = sim_x;
  path_y[path_n & 31u] = sim_y;
  path_n++;
  for (k = 0; k < 4u; k++) {
    uint8_t nx = (uint8_t)(sim_x + DX[k]), ny = (uint8_t)(sim_y + DY[k]);
    if (k == ((sim_d + 2u) & 3u)) continue;
    if (!at(nx, ny) && !is_dead(nx, ny)) opts[n++] = k;
  }
  if (!n)
    d = (uint8_t)((sim_d + 2u) & 3u);
  else {
    d = opts[rnd_n(n)];
    for (k = 0; k < n; k++)
      if (opts[k] == sim_d && (rnd() & 3u)) d = sim_d;
  }
  sim_d = d;
  sim_x = (uint8_t)(sim_x + DX[d]);
  sim_y = (uint8_t)(sim_y + DY[d]);
}

static uint8_t path_dir(uint8_t k) {
  uint8_t a = k & 31u, b = (uint8_t)(k + 1u) & 31u;
  if (path_x[b] > path_x[a]) return 0;
  if (path_y[b] > path_y[a]) return 1;
  if (path_x[b] < path_x[a]) return 2;
  return 3;
}

static void path_pos(uint8_t k, uint8_t frac, uint16_t *ox, uint16_t *oy) {
  uint8_t a = k & 31u, d = path_dir(k);
  uint16_t x = (uint16_t)(((uint16_t)path_x[a] << 8) | 128u), y = (uint16_t)(((uint16_t)path_y[a] << 8) | 128u);
  if (d == 0) x += frac;
  else if (d == 1) y += frac;
  else if (d == 2) x -= frac;
  else y -= frac;
  *ox = x;
  *oy = y;
}

static void walker_init(void) {
  uint8_t i, sx, sy, n = 0, pick = rnd();
  /* start on a seeded live cell */
  sim_x = 1;
  sim_y = 1;
  for (sy = 1; sy < 30u; sy++)
    for (sx = 1; sx < 30u; sx++)
      if (!at(sx, sy) && !is_dead(sx, sy) && n++ <= pick) {
        sim_x = sx;
        sim_y = sy;
      }
  sim_d = 0;
  for (i = 0; i < 4u; i++)
    if (!at((uint8_t)(sim_x + DX[i]), (uint8_t)(sim_y + DY[i])) &&
        !is_dead((uint8_t)(sim_x + DX[i]), (uint8_t)(sim_y + DY[i])))
      sim_d = i;
  path_n = 0;
  for (i = 0; i < AHEAD + 3u; i++) path_push();
  wk = 0;
  wfrac = 0;
  t3d_ca = (uint16_t)path_dir(0) << 8;
}

static void walker_step(void) {
  uint16_t target = (uint16_t)path_dir(wk) << 8;
  if (t3d_ca != target) {
    uint16_t diff = (uint16_t)((target - t3d_ca) & 1023u);
    if (diff <= TURN_SPD || diff >= 1024u - TURN_SPD) t3d_ca = target;
    else if (diff < 512u) t3d_ca = (uint16_t)((t3d_ca + TURN_SPD) & 1023u);
    else t3d_ca = (uint16_t)((t3d_ca - TURN_SPD) & 1023u);
    return;
  }
  if ((uint16_t)wfrac + MOVE_SPD >= 256u) {
    wfrac = (uint8_t)(wfrac + MOVE_SPD);
    wk++;
    while ((uint8_t)(path_n - wk) < AHEAD + 3u) path_push();
  } else {
    wfrac = (uint8_t)(wfrac + MOVE_SPD);
  }
}

static uint16_t trail(uint16_t a) {
  uint16_t v = (uint16_t)(((uint16_t)t3d_cosabs(a) * TRAIL) >> 8);
  return ((a & 1023u) < 256u || (a & 1023u) >= 768u) ? v : (uint16_t)-v;
}

static void walker_update(uint8_t dt) {
  while (dt--) walker_step();
  path_pos(wk, wfrac, &t3d_cx, &t3d_cy);
  t3d_cx -= trail(t3d_ca);
  t3d_cy -= trail((uint16_t)(t3d_ca - 256u));
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
static uint8_t vlevel;     /* the view, from the backdrop colour 0..16 (it comes up once the world is generated) */
static uint8_t flick;      /* 0 lit .. 16 the dark palette */
static uint8_t logo_state; /* 0 off, 1 dim, 2 on, 3 flash, 4 chroma-swapped */
static uint8_t blink_on = 1;

static uint16_t fade(uint16_t c) { return level >= 16u ? c : mix555(BLACK, c, level); }

static void pal_build(void) {
  uint8_t i;
  const uint16_t *lit = &T3D_BPAL[PAL_OFFICE * 32u], *dk = &T3D_BPAL[PAL_DARK * 32u];
  for (i = 0; i != 32u; i++) {
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
    t3d_palx[32u + i] = fade(c);
    t3d_palx[36u + i] = fade(i == 1u && !blink_on ? T3D_TEXT_PAL[0] : T3D_TEXT_PAL[i]);
  }
  t3d_pal_dirty = 1;
}

/* ---- VRAM ---- */
static uint8_t row[32];

static void vset(uint16_t at, const uint8_t *src, uint16_t n) { set_data((uint8_t *)at, src, n); }

/* both BG maps, one row: tile and attribute */
static void map_row(uint8_t y, const uint8_t *tiles, const uint8_t *attr0, const uint8_t *attr1) {
  VBK_REG = 0;
  vset(0x9800u + (uint16_t)y * 32u, tiles, 32);
  vset(0x9C00u + (uint16_t)y * 32u, tiles, 32);
  VBK_REG = 1;
  vset(0x9800u + (uint16_t)y * 32u, attr0, 32);
  vset(0x9C00u + (uint16_t)y * 32u, attr1, 32);
  VBK_REG = 0;
}

static const char font_order[] = T3D_FONT_ORDER;
static uint8_t glyph(char c) {
  uint8_t i;
  for (i = 0; font_order[i]; i++)
    if (font_order[i] == c) return i;
  return 0;
}

/* text into both maps (tiles only: the attributes are set once); ink 1 white, 2 lavender */
static void text(uint8_t x, uint8_t y, const char *s, uint8_t ink) {
  uint8_t n = 0, base = ink == 1u ? T3D_T_FONT1 : T3D_T_FONT2;
  while (s[n] && x + n < 20u) {
    row[n] = (uint8_t)(base + glyph(s[n]));
    n++;
  }
  VBK_REG = 0;
  vset(0x9800u + (uint16_t)y * 32u + x, row, n);
  vset(0x9C00u + (uint16_t)y * 32u + x, row, n);
}

static void video_init(void) {
  uint8_t x, y, t;
  static uint8_t a0[32], a1[32];
  /* framebuffer tiles 0-95 in both banks: zero (bank 2's buffer is zero right now and serves as the source) */
  memset((uint8_t *)T3D_FB, 0, T3D_BUFSZ);
  VBK_REG = 0;
  vset(0x8000u, (const uint8_t *)T3D_FB, T3D_BUFSZ);
  vset(0x8000u + T3D_T_LOGO * 16u, T3D_LOGO_TILES, sizeof T3D_LOGO_TILES);
  vset(0x8000u + T3D_T_FRAME * 16u, T3D_FRAME_TILES, sizeof T3D_FRAME_TILES);
  VBK_REG = 1;
  vset(0x8000u, (const uint8_t *)T3D_FB, T3D_BUFSZ);
  vset(0x8000u + T3D_T_FONT1 * 16u, T3D_FONT1, sizeof T3D_FONT1);
  vset(0x8000u + T3D_T_FONT2 * 16u, T3D_FONT2, sizeof T3D_FONT2);
  VBK_REG = 0;
  for (y = 0; y != 32u; y++) {
    for (x = 0; x != 32u; x++) {
      uint8_t in = x >= T3D_VX && x < T3D_VX + T3D_TW && y >= T3D_VY && y < T3D_VY + T3D_TH;
      uint8_t pal, k = 0;
      if (in) {
        t = (uint8_t)((x - T3D_VX) * T3D_TH + (y - T3D_VY));
        pal = (uint8_t)(y - T3D_VY);
        a0[x] = pal;
        a1[x] = (uint8_t)(pal | 0x08u);
        row[x] = t;
        continue;
      }
      if (y < T3D_VY) {
        /* the logo (rows 1-3) and the top bar, palette 7 */
        pal = 7u;
        if (y >= 1u && y <= 3u && x < 20u) t = (uint8_t)(T3D_T_LOGO + T3D_LOGO_MAP[(y - 1u) * 20u + x]);
        else if (y == T3D_VY - 1u && x == T3D_VX - 1u) t = T3D_T_FRAME + 6u;
        else if (y == T3D_VY - 1u && x == T3D_VX + T3D_TW) t = T3D_T_FRAME + 7u;
        else if (y == T3D_VY - 1u && x >= T3D_VX && x < T3D_VX + T3D_TW) t = T3D_T_FRAME + 4u;
        else t = T3D_T_FRAME;
      } else if (y < T3D_VY + T3D_TH) {
        /* beside the view: the row's own palette, its dark colour */
        pal = (uint8_t)(y - T3D_VY);
        if (x == T3D_VX - 1u) k = 2u;
        else if (x == T3D_VX + T3D_TW) k = 3u;
        else k = ((x + y) & 3u) == 0u ? 10u : 1u;
        t = (uint8_t)(T3D_T_FRAME + k);
      } else {
        /* the bottom bar and the text rows, palette 0 */
        pal = 0u;
        if (y == T3D_VY + T3D_TH && x == T3D_VX - 1u) t = T3D_T_FRAME + 8u;
        else if (y == T3D_VY + T3D_TH && x == T3D_VX + T3D_TW) t = T3D_T_FRAME + 9u;
        else if (y == T3D_VY + T3D_TH && x >= T3D_VX && x < T3D_VX + T3D_TW) t = T3D_T_FRAME + 5u;
        else if (y >= 14u && y < 18u && x < 20u) {
          t = T3D_T_FONT1; /* a space of the font, bank 1 */
          pal = 0x08u;
        } else
          t = T3D_T_FRAME;
      }
      row[x] = t;
      a0[x] = pal;
      a1[x] = pal;
    }
    map_row(y, row, a0, a1);
  }
  SCX_REG = 0;
  SCY_REG = 0;
}

static const char HEX[] = "0123456789ABCDEF";
static void seed_line(void) {
  static char s[17];
  memcpy(s, "SEED 0000 * KEEL", 17);
  s[5] = HEX[t3d_seed >> 12];
  s[6] = HEX[(t3d_seed >> 8) & 15u];
  s[7] = HEX[(t3d_seed >> 4) & 15u];
  s[8] = HEX[t3d_seed & 15u];
  text(2, 17, s, 2);
}

static void clock_line(uint8_t phase) {
  static char s[19];
  clock_title_text(phase, s);
  text(1, 16, s, 2);
}

/* ---- entropy for the seed ---- */
static uint16_t entropy(void) {
  /* WRAM bank 7 is never written by the game: on hardware it holds the power-on noise of the RAM cells */
  uint16_t h = 0x1D3Bu;
  const uint8_t *p = (const uint8_t *)0xD000u;
  uint16_t i;
  SVBK_REG = 7;
  for (i = 0; i < 0x1000u; i += 7u) h = (uint16_t)((h << 5) + (h >> 11) + p[i]);
  SVBK_REG = T3D_WBANK_A;
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

void title3d_main(void) BANKED {
  uint8_t cur = 0, lcd_was_on, lcdc0, i, dt, out = 0, k;
  uint16_t lastv, v, out_t = 0, clock_t;
  uint8_t *h = (uint8_t *)(0xFF00u + T3D_HRAM_LO);

  t3d_wbank = T3D_WBANK_A;
  SVBK_REG = T3D_WBANK_A;
  for (i = 0; i != T3D_HRAM_N; i++) T3D_HSAVE[i] = h[i];
  if (!titles) t3d_seed ^= entropy();
  if (t3d_seed_force) t3d_seed = t3d_seed_force;
  if (!t3d_seed) t3d_seed = 0x5EEDu;
  titles++;

  /* video under black palettes (the LCD may be on: after the KEEL boot it stays on, black) */
  level = 0;
  vlevel = 0;
  flick = 0;
  logo_state = 1; /* the tube is there, unlit, until the walk starts */
  blink_on = 1;
  logo_t = 0;
  glitch_left = flick_left = 0;
  next_glitch = 400u;
  t3d_glitch = 0;
  t3d_pend = 0;
  t3d_freeze = t3d_frozen = 0;
  pal_build();
  lcd_was_on = (LCDC_REG & LCDCF_ON) ? 1u : 0u;
  lcdc0 = LCDC_REG;
  if (lcd_was_on) {
    wait_vbl_done();
    BCPS_REG = 0x80u;
    for (i = 0; i != 64u; i++) BCPD_REG = 0;
  }
  LCDC_REG = (uint8_t)((LCDC_REG | LCDCF_BG8000 | LCDCF_BGON) & (uint8_t)~(LCDCF_BG9C00 | LCDCF_WINON));
  video_init();
  seed_line();
  text(4, 14, "PRESS START", 1);
  clock_line(0);
  t3d_cast_init();
  t3d_nviews = t3d_nflips = 0;
  t3d_vbl = 0;
  t3d_isr_on();
  if (!lcd_was_on) DISPLAY_ON;
  sound_call(0);
  /* the frame, the logo's dark tube and the text fade in while the world is generated (~1.3 s of SDCC code) */
  while (level < 16u) {
    wait_vbl_done();
    level = (uint8_t)(level + 2u);
    pal_build();
  }
  /* then the world: maze, dressing, walker (bank 2), then the map and dead bits into bank 3 */
  rng = t3d_seed;
  gen_maze();
  dress();
  walker_init();
  for (k = 0; k != (1024u + 128u) / 32u; k++) {
    uint8_t *p = k < 32u ? map + (uint16_t)k * 32u : T3D_DEAD + (uint16_t)(k - 32u) * 32u;
    memcpy(row, p, 32);
    SVBK_REG = T3D_WBANK_B;
    memcpy(p, row, 32);
    SVBK_REG = T3D_WBANK_A;
  }

  next_flick = (uint16_t)(90u + (rnd() & 63u));

  lastv = t3d_vbl;
  clock_t = 0;
  t3d_result = 0;
  while (1) {
    if (t3d_freeze) {
      while (t3d_pend) {
      }
      t3d_frozen = 1;
      while (t3d_freeze) {
      }
      t3d_frozen = 0;
      lastv = t3d_vbl;
    }
    v = t3d_vbl;
    dt = (uint8_t)((uint16_t)(v - lastv) > 8u ? 8u : (v - lastv));
    lastv = v;
    walker_update(dt);
    logo_update(dt);
    flicker_update(dt);
    if (vlevel < 16u && !out) {
      vlevel = (uint8_t)(vlevel + dt > 16u ? 16u : vlevel + dt);
      pal_build();
    }
    if (((v >> 5) & 1u) != (uint8_t)!blink_on && !out) {
      blink_on = (uint8_t)!((v >> 5) & 1u);
      pal_build();
    }
    /* render into this view's bank */
    t3d_wbank = (uint8_t)(T3D_WBANK_A + cur);
    SVBK_REG = t3d_wbank;
    t3d_render();
    t3d_last_bank = t3d_wbank;
    while (t3d_pend) {
    }
    t3d_pend_bank = t3d_wbank;
    t3d_pend = 1;
    t3d_nviews++;
    cur ^= 1u;
    /* the clock row, about once a second */
    clock_t += dt;
    if (clock_t >= 64u && !out) {
      clock_t = 0;
      clock_line((uint8_t)(v >> 6));
    }
    /* START (or A) plays; SELECT opens SETUP; both fade out over the walk */
    if (!out) {
      uint8_t j = vlevel >= 16u ? t3d_keys : 0u; /* a press while the world builds waits for the view */
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
  /* leave: the last palettes black, the game's video and interrupts back, the HRAM and WRAM bank 1 back */
  while (t3d_pend) {
  }
  wait_vbl_done();
  wait_vbl_done();
  t3d_sound = 0;
  t3d_isr_off();
  BCPS_REG = 0x80u;
  for (i = 0; i != 64u; i++) BCPD_REG = 0;
  LCDC_REG = (uint8_t)((LCDC_REG & (uint8_t)~(LCDCF_BG8000 | LCDCF_BG9C00)) |
                       (lcdc0 & (uint8_t)(LCDCF_BG8000 | LCDCF_BG9C00)));
  VBK_REG = 0;
  SVBK_REG = T3D_WBANK_A;
  for (i = 0; i != T3D_HRAM_N; i++) h[i] = T3D_HSAVE[i];
  t3d_wbank = 1;
  SVBK_REG = 1;
  t3d_result = out;
}

uint8_t title3d_run(void) BANKED {
  memset(t3d_stack, 0xA5, T3D_STACK); /* the harness reads the high-water mark */
  t3d_seed = (uint16_t)(t3d_seed * 0x6255u + 0x3619u) ^ DIV_REG ^ (frame_count << 5) ^ titles;
  t3d_on_stack();
  return t3d_result;
}
