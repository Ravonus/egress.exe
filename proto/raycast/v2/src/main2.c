/* EGRESS.EXE raycaster v2 prototype (GB side). Same maze, walker, framebuffer and flip scheme as v1
   (proto/raycast/src/main.c) so the views compare one to one; v2 adds textured + fog-dithered walls, cast
   floor/ceiling with light pools, per-biome palettes, and (scenes 1-3) OBJ billboards and world changes. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include <string.h>
#include "ray2.h"
#include "tex_ids.h"
#include "obj2.h"
#if OBJ_ON
#include "bb_data.h"
#endif

#ifndef SCENE
#define SCENE 0 /* 0 walk (v1 views), 1 combat, 2 puzzle, 3 biome blend, 4 title walk */
#endif

#define VX ((20 - TW) / 2)
#define VY ((18 - TH) / 2)
#define HUDT 200
#define FRAMET (HUDT + 20)

#define MOVE_SPD 4u
#define TURN_SPD 6u
#define AHEAD 4u

volatile uint16_t vbl;
volatile uint8_t pend;
volatile uint8_t freeze, frozen;
volatile uint8_t cam_lock; /* measure2.py pokes the camera and sets this for same-view stills */
uint8_t pend_chunk, front;
uint8_t *pend_src;
uint16_t pend_view;
uint8_t *buf[2];
uint8_t cur, last_buf;
uint8_t pad_n;
uint8_t hud_t[2];
uint8_t hud_dirty;
uint8_t pal_dirty;
uint16_t pal_cur[32];

#define LOGN 64
uint16_t log_flip[LOGN];
/* per-phase lines, one byte each (saturated): WRAM is full once the billboard staging area is in */
uint8_t log_upd[LOGN], log_ray[LOGN], log_fill[LOGN], log_flr[LOGN], log_spr[LOGN], log_wait[LOGN];
const uint8_t log_u8 = 1;
#define LSAT(v) ((v) > 255u ? 255u : (uint8_t)(v))
uint16_t nviews, nflips, copy_lines, copy_calls;

void dma_pad(void) __naked {
  __asm
    ld a, (#_pad_n)
    or a, a
    ret z
    ld b, a
  09901$:
    ld c, #3
  09902$:
    dec c
    jr nz, 09902$
    dec b
    jr nz, 09901$
    ret
  __endasm;
}

static void gdma(uint16_t src, uint16_t dst, uint8_t blocks) {
  HDMA1_REG = (uint8_t)(src >> 8);
  HDMA2_REG = (uint8_t)src;
  HDMA3_REG = (uint8_t)(dst >> 8);
  HDMA4_REG = (uint8_t)dst;
  HDMA5_REG = (uint8_t)(blocks - 1u);
#ifndef NO_DMA_PAD
  pad_n = blocks;
  dma_pad();
#endif
}

/* BG palettes straight to the registers: library code above 0x4000 must not run from the ISR (a data bank may be
   mapped there while the fill asm runs) */
static void bkpal_write(void) {
  uint8_t i;
  BCPS_REG = 0x80u;
  for (i = 0; i < 32u; i++) {
    BCPD_REG = (uint8_t)pal_cur[i];
    BCPD_REG = (uint8_t)(pal_cur[i] >> 8);
  }
}

void vbl_isr(void) {
  uint8_t l0;
  vbl++;
  if (hud_dirty) {
    VBK_REG = 0;
    *(volatile uint8_t *)(0x9800u + (17u * 32u) + 15u) = hud_t[0];
    *(volatile uint8_t *)(0x9800u + (17u * 32u) + 16u) = hud_t[1];
    *(volatile uint8_t *)(0x9C00u + (17u * 32u) + 15u) = hud_t[0];
    *(volatile uint8_t *)(0x9C00u + (17u * 32u) + 16u) = hud_t[1];
    hud_dirty = 0;
  }
  if (!pend) {
    if (pal_dirty) {
      bkpal_write();
      pal_dirty = 0;
    }
    return;
  }
  l0 = LY_REG;
  if (pend_chunk == 0) {
    /* chunk 0: the BG framebuffer into the hidden VRAM bank */
    VBK_REG = front ^ 1u;
    gdma((uint16_t)pend_src, 0x8000u, BUFSZ / 16);
    copy_lines += (uint8_t)(LY_REG - l0);
    copy_calls++;
#if OBJ_ON
    pend_chunk = 1;
    return;
#endif
  }
#if OBJ_ON
  else {
    /* chunk 1: this view's billboard tiles into the hidden bank, then OAM + OBJ palettes */
    VBK_REG = front ^ 1u;
    obj_vbl_upload();
    copy_lines += (uint8_t)(LY_REG - l0);
    copy_calls++;
  }
#endif
  pend_chunk = 0;
  front ^= 1u;
  LCDC_REG ^= LCDCF_BG9C00;
#if OBJ_ON
  obj_vbl_flip();
#endif
  if (pal_dirty) {
    bkpal_write();
    pal_dirty = 0;
  }
  log_flip[pend_view & (LOGN - 1)] = vbl;
  nflips++;
  pend = 0;
}

static uint16_t read_vbl(void) {
  uint16_t v;
  do {
    v = vbl;
  } while (v != vbl);
  return v;
}

static uint16_t now(void) {
  uint16_t v;
  uint8_t ly;
  do {
    v = vbl;
    ly = LY_REG;
  } while (v != vbl);
  if (ly >= 144u) return (uint16_t)(v * 154u + ly - 154u);
  return (uint16_t)(v * 154u + ly);
}

/* ---- maze (v1, unchanged: same seed, same geometry) ---- */
static uint16_t rng = 0x5EEDu;
static uint8_t rnd(void) {
  rng ^= rng << 7;
  rng ^= rng >> 9;
  rng ^= rng << 8;
  return (uint8_t)rng;
}

static const int8_t DX[4] = {1, 0, -1, 0};
static const int8_t DY[4] = {0, 1, 0, -1};
static uint8_t stk[225];

static uint8_t at(uint8_t x, uint8_t y) {
  uint8_t c = map[(uint16_t)y * MAPW + x];
  return c < 0x80u ? c : 0u;
}

static uint8_t dead[128];
static uint8_t is_dead(uint8_t x, uint8_t y) {
  uint16_t i = (uint16_t)y * MAPW + x;
  return dead[i >> 3] & (uint8_t)(1u << (i & 7u));
}
static void set_dead(uint8_t x, uint8_t y) {
  uint16_t i = (uint16_t)y * MAPW + x;
  dead[i >> 3] |= (uint8_t)(1u << (i & 7u));
}

static void gen_maze(void) {
  uint16_t i;
  uint8_t sp = 0, x, y, n, k, d, dirs[4];
  for (i = 0; i < MAPW * MAPW; i++) map[i] = 1;
  x = 1;
  y = 1;
  map[MAPW + 1] = 0;
  stk[sp++] = 0;
  while (sp) {
    k = stk[sp - 1];
    x = (uint8_t)((k % 15u) * 2u + 1u);
    y = (uint8_t)((k / 15u) * 2u + 1u);
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
    d = dirs[rnd() % n];
    map[(uint16_t)(y + DY[d]) * MAPW + (uint8_t)(x + DX[d])] = 0;
    x = (uint8_t)(x + DX[d] * 2);
    y = (uint8_t)(y + DY[d] * 2);
    map[(uint16_t)y * MAPW + x] = 0;
    stk[sp++] = (uint8_t)((y >> 1) * 15u + (x >> 1));
  }
  for (k = 0; k < 28u; k++) {
    x = (uint8_t)(rnd() % 29u + 1u);
    y = (uint8_t)(rnd() % 29u + 1u);
    if (((x ^ y) & 1u) == 0) continue;
    if ((x & 1u) == 0 && !at(x - 1u, y) && !at(x + 1u, y)) map[(uint16_t)y * MAPW + x] = 0;
    if ((y & 1u) == 0 && !at(x, y - 1u) && !at(x, y + 1u)) map[(uint16_t)y * MAPW + x] = 0;
  }
  for (y = 1; y < 30u; y++)
    for (x = 1; x < 30u; x++) {
      uint8_t cx = x, cy = y;
      while (!at(cx, cy) && !is_dead(cx, cy)) {
        uint8_t live = 0, lx = 0, ly = 0;
        for (d = 0; d < 4; d++) {
          uint8_t nx = (uint8_t)(cx + DX[d]), ny = (uint8_t)(cy + DY[d]);
          if (!at(nx, ny) && !is_dead(nx, ny)) {
            live++;
            lx = nx;
            ly = ny;
          }
        }
        if (live > 1u) break;
        set_dead(cx, cy);
        if (!live) break;
        cx = lx;
        cy = ly;
      }
    }
  for (y = 1; y < 30u; y++)
    for (x = 1; x < 30u; x++) {
      uint8_t open = 0, od = 0, live = 0;
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

/* ---- v2 dressing: textures and floor sets from a separate seeded hash, so the maze and walk stay v1's ---- */
static uint8_t hash8(uint16_t i, uint8_t salt) {
  uint16_t h = (uint16_t)(i * 0x9E37u) ^ (uint16_t)(salt * 0x45D9u);
  h ^= h >> 7;
  h *= 0x2F1Bu;
  h ^= h >> 9;
  return (uint8_t)h;
}

static const uint8_t O_DECOR[] = {O_OUTLET, O_OUTLET, O_POSTER, O_VENT, O_CRT, O_STAIN, O_WATER, O_CLOCK, O_EXIT,
                                  O_POSTER, O_STAIN, O_OUTLET};
static const uint8_t V_DECOR[] = {V_GRATE, V_FAN, V_PIPES, V_HAZARD, V_SIGN, V_STEAM, V_LADDER, V_PIPES};

/* biome by cell column: office, a 3-cell dithered threshold, vents (scene 3); everything else is office */
static uint8_t biome_frac(uint8_t x) {
#if SCENE == 3
  if (x < 14u) return 0;
  if (x > 16u) return 4;
  return (uint8_t)(x - 13u); /* 1..3 */
#else
  (void)x;
  return 0;
#endif
}

static void dress(void) {
  uint8_t x, y;
  for (y = 0; y < MAPW; y++)
    for (x = 0; x < MAPW; x++) {
      uint16_t i = (uint16_t)y * MAPW + x;
      uint8_t c = map[i], h = hash8(i, 1), zone = hash8((uint16_t)((y >> 3) * 4u + (x >> 3)), 2);
      uint8_t bf = biome_frac(x);
      uint8_t dark = (hash8((uint16_t)((y >> 2) * 8u + (x >> 2)), 3) & 7u) == 0;
      /* floor / ceiling set */
      if (!c) {
        if (bf == 0) map[i] = (uint8_t)(0x80u | (dark ? S_OFFDARK : S_OFFICE));
        else if (bf == 4) map[i] = (uint8_t)(0x80u | S_VENTS);
        else map[i] = (uint8_t)(0x80u | (S_BLEND4 + bf - 1u));
        continue;
      }
      if (c == 2) {
        map[i] = (h & 7u) == 0 ? (bf >= 3u ? V_HATCH : O_DOOREXIT) : (bf >= 3u ? V_HATCH : O_DOOR);
        continue;
      }
      if (bf == 4u) {
        map[i] = (h < 80u) ? V_DECOR[h % sizeof V_DECOR] : (uint8_t)(V_BASE0 + zone % 3u);
      } else if (bf) {
        map[i] = (uint8_t)(B_OV4 + bf - 1u);
      } else if (h < 4u) {
        map[i] = O_TEAR;
      } else if (h < 92u) {
        map[i] = O_DECOR[h % sizeof O_DECOR];
      } else {
        map[i] = (uint8_t)(O_BASE0 + zone % 3u);
      }
    }
}

/* ---- right-hand walker (v1) ---- */
static uint8_t path_x[32], path_y[32];
static uint8_t sim_x = 1, sim_y = 1, sim_d = 0, path_n;
static uint8_t wk;
static uint8_t wfrac;

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
  if (!n) d = (uint8_t)((sim_d + 2u) & 3u);
  else {
    d = opts[rnd() % n];
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
  uint8_t i;
  for (sim_y = 1; sim_y < 30u; sim_y++) {
    for (sim_x = 1; sim_x < 30u; sim_x++)
      if (!at(sim_x, sim_y) && !is_dead(sim_x, sim_y)) break;
    if (sim_x < 30u) break;
  }
  for (i = 0; i < 4u; i++)
    if (!at((uint8_t)(sim_x + DX[i]), (uint8_t)(sim_y + DY[i])) &&
        !is_dead((uint8_t)(sim_x + DX[i]), (uint8_t)(sim_y + DY[i])))
      sim_d = i;
  for (i = 0; i < AHEAD + 3u; i++) path_push();
  wk = 0;
  wfrac = 0;
  cam_a = (uint16_t)path_dir(0) << 8;
}

static void walker_step(void) {
  uint16_t target = (uint16_t)path_dir(wk) << 8;
  if (cam_a != target) {
    uint16_t diff = (uint16_t)((target - cam_a) & 1023u);
    if (diff <= TURN_SPD || diff >= 1024u - TURN_SPD) cam_a = target;
    else if (diff < 512u) cam_a = (uint16_t)((cam_a + TURN_SPD) & 1023u);
    else cam_a = (uint16_t)((cam_a - TURN_SPD) & 1023u);
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

#define TRAIL 96u
static uint16_t trail(uint16_t a) {
  uint16_t v = (uint16_t)(((uint16_t)T_COSABS[a & 1023u] * TRAIL) >> 8);
  return ((a & 1023u) < 256u || (a & 1023u) >= 768u) ? v : (uint16_t)-v;
}

uint16_t spr_x, spr_y; /* v1's walker figure (scene 0 keeps it as one billboard) */
static void walker_update(uint8_t dt) {
  if (cam_lock) return;
  while (dt--) walker_step();
  path_pos(wk, wfrac, &cam_x, &cam_y);
  cam_x -= trail(cam_a);
  cam_y -= trail((uint16_t)(cam_a - 256u));
  path_pos((uint8_t)(wk + AHEAD), wfrac, &spr_x, &spr_y);
}

/* ---- palettes: per-row BG palettes from a biome, crossfaded (t 0..16) ---- */
static uint16_t mix555(uint16_t a, uint16_t b, uint8_t t) {
  uint8_t r = (uint8_t)(((a & 31u) * (16u - t) + (b & 31u) * t) >> 4);
  uint8_t g = (uint8_t)((((a >> 5) & 31u) * (16u - t) + ((b >> 5) & 31u) * t) >> 4);
  uint8_t bl = (uint8_t)((((a >> 10) & 31u) * (16u - t) + ((b >> 10) & 31u) * t) >> 4);
  return (uint16_t)(r | ((uint16_t)g << 5) | ((uint16_t)bl << 10));
}

void pal_blend(uint8_t pa, uint8_t pb, uint8_t t) {
  uint8_t i;
  const uint16_t *a = &T_BPAL[(uint16_t)pa * 32u], *b = &T_BPAL[(uint16_t)pb * 32u];
  for (i = 0; i < 32u; i++) pal_cur[i] = t ? mix555(a[i], b[i], t) : a[i];
  pal_dirty = 1;
}

static const char TITLE[] = "EGRESS.EXE";
static const char CHARS[] = "0123456789EGRSX.FP/ ";

static uint8_t char_tile(char c) {
  uint8_t i;
  for (i = 0; CHARS[i]; i++)
    if (CHARS[i] == c) return (uint8_t)(HUDT + i);
  return HUDT + 19u;
}

static const palette_color_t HUDPAL[4] = {RGB8(232, 214, 244), RGB8(250, 204, 222), RGB8(178, 164, 230),
                                          RGB8(82, 78, 148)};

static void video_init(void) {
  uint8_t x, y, t, a;
  uint8_t zero[16];
  for (x = 0; x < 16u; x++) zero[x] = 0;
  pal_blend(SCENE == 3 ? PAL_OFFICE : PAL_OFFICE, PAL_OFFICE, 0);
  set_bkg_palette(0, 8, pal_cur);
  pal_dirty = 0;
  VBK_REG = 0;
  set_bkg_data(HUDT, 29, T_HUDTILES);
  for (t = 0; t < TW * TH; t++) set_bkg_data(t, 1, zero);
  VBK_REG = 1;
  for (t = 0; t < TW * TH; t++) set_bkg_data(t, 1, zero);
  for (y = 0; y < 18u; y++)
    for (x = 0; x < 20u; x++) {
      uint16_t o = (uint16_t)y * 32u + x;
      uint8_t in = x >= VX && x < VX + TW && y >= VY && y < VY + TH;
      if (in) {
        t = (uint8_t)((x - VX) * TH + (y - VY));
        a = (uint8_t)((y - VY) * 8u / TH);
      } else {
        uint8_t k = 0;
        if (y == VY - 1u && x >= VX && x < VX + TW) k = 1;
        else if (y == VY + TH && x >= VX && x < VX + TW) k = 2;
        else if (x == VX - 1u && y >= VY && y < VY + TH) k = 3;
        else if (x == VX + TW && y >= VY && y < VY + TH) k = 4;
        else if (x == VX - 1u && y == VY - 1u) k = 5;
        else if (x == VX + TW && y == VY - 1u) k = 6;
        else if (x == VX - 1u && y == VY + TH) k = 7;
        else if (x == VX + TW && y == VY + TH) k = 8;
        t = (uint8_t)(FRAMET + k);
        a = 0;
        if (y == 1u && x >= 5u && x < 15u) t = char_tile(TITLE[x - 5u]);
        if (y == 17u && x >= 11u && x < 17u) t = char_tile("FPS   "[x - 11u]);
      }
      VBK_REG = 0;
      *(uint8_t *)(0x9800u + o) = t;
      *(uint8_t *)(0x9C00u + o) = t;
      VBK_REG = 1;
      *(uint8_t *)(0x9800u + o) = in ? a : 0u;
      *(uint8_t *)(0x9C00u + o) = in ? (uint8_t)(a | 0x08u) : 0u;
    }
  VBK_REG = 0;
  SCX_REG = 0;
  SCY_REG = 0;
}

/* ---- scenes ---- */
#include "scenes2.h"

void main(void) {
  uint16_t t0, t1, t2, t2b, t3, t4, t5, lastv, v;
  uint8_t slot;
  DISPLAY_OFF;
  cpu_fast();
  buf[0] = (uint8_t *)0xCC00u; /* page-aligned framebuffers below the map (0xD800); DATA stays under 0xCB00 */
  buf[1] = (uint8_t *)0xD200u;
  gen_maze();
  walker_init();
  dress();
  scene_init();
  video_init();
#ifdef CAST_ASM
  { extern void cast_init(void); cast_init(); }
#endif
  obj_init();
  LCDC_REG = (uint8_t)((LCDC_REG | LCDCF_BG8000 | LCDCF_BGON) & (uint8_t)~LCDCF_BG9C00);
  front = 0;
  disable_interrupts();
  add_VBL(vbl_isr);
  set_interrupts(VBL_IFLAG);
  enable_interrupts();
  DISPLAY_ON;
  lastv = read_vbl();
  for (;;) {
    uint8_t dt;
    if (freeze) {
      frozen = 1;
      while (freeze) {
      }
      frozen = 0;
      lastv = read_vbl();
    }
    t0 = now();
    v = read_vbl();
    dt = (uint8_t)((v - lastv) > 8u ? 8u : (v - lastv));
    lastv = v;
    scene_update(dt);
    t1 = now();
    cast_all();
    t2 = now();
    walls_all(buf[cur]);
    t2b = now();
    
#ifndef NOFLOOR
    floors_all(buf[cur]);
#endif

    t3 = now();
    while (pend) { /* the staging area and OAM buffer are free once the previous view is up */
    }
    t4 = now();
    obj_build(cur);
    t5 = now();
    slot = (uint8_t)(nviews & (LOGN - 1));
    log_upd[slot] = LSAT((uint16_t)(t1 - t0));
    log_ray[slot] = LSAT((uint16_t)(t2 - t1));
    log_fill[slot] = LSAT((uint16_t)(t2b - t2));
    log_flr[slot] = LSAT((uint16_t)(t3 - t2b));
    log_spr[slot] = LSAT((uint16_t)(t5 - t4));
    log_wait[slot] = LSAT((uint16_t)(t4 - t3));
    pend_src = buf[cur];
    pend_view = nviews;
    pend = 1;
    last_buf = cur;
    cur ^= 1u;
    nviews++;
    if (nflips > 8u) {
      uint16_t span = (uint16_t)(log_flip[(nflips - 1u) & (LOGN - 1)] - log_flip[(nflips - 9u) & (LOGN - 1)]);
      uint16_t fps = span ? (uint16_t)((8u * 60u + span / 2u) / span) : 0;
      if (fps > 99u) fps = 99u;
      hud_t[0] = (uint8_t)(HUDT + fps / 10u);
      hud_t[1] = (uint8_t)(HUDT + fps % 10u);
      hud_dirty = 1;
    }
  }
}
