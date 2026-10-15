/* EGRESS.EXE raycaster feasibility prototype (GB side): CGB double speed, tile framebuffer, WRAM render ->
   VBlank general-purpose DMA into the hidden VRAM bank -> flip the BG map (LCDC bit 3). */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include "ray.h"

#define VX ((20 - TW) / 2)
#define VY ((18 - TH) / 2)
#define NCHUNK (BUFSZ > 1792 ? 2 : 1)
#define CHUNK (BUFSZ / NCHUNK)
#define HUDT 200
#define FRAMET (HUDT + 20)

#define MOVE_SPD 4u /* 8.8 cells per vblank: one cell in 64 vblanks */
#define TURN_SPD 6u /* angle units per vblank: 90 degrees in ~43 vblanks */
#define AHEAD 4u /* the figure walks this many path cells ahead of the camera */

volatile uint16_t vbl;
volatile uint8_t pend;
volatile uint8_t freeze, frozen;
volatile uint8_t cam_lock; /* measure tools: hold the camera they wrote (same-view stills) */
uint8_t pend_chunk, front;
uint8_t *pend_src;
uint16_t pend_view;
uint8_t bufmem[2 * BUFSZ + 16];
uint8_t *buf[2];
uint8_t cur, last_buf;
uint8_t pad_n;
uint8_t hud_t[2];
uint8_t hud_dirty;

/* per-view measurement log (ring), in scanlines (1/154 frame) */
#define LOGN 64
uint16_t log_flip[LOGN];
uint16_t log_upd[LOGN], log_ray[LOGN], log_fill[LOGN], log_spr[LOGN], log_wait[LOGN];
uint16_t nviews, nflips, copy_lines, copy_calls;

/* GDMA halts the CPU ~8 us per 16-byte block on hardware; PyBoy copies for free, so the emulator build burns the
   same time here (17 double-speed M-cycles per block) to keep the measurement honest. */
void dma_pad(void) __naked {
  __asm
    ld a, (#_pad_n)
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

void vbl_isr(void) {
  vbl++;
  if (hud_dirty) {
    VBK_REG = 0;
    *(volatile uint8_t *)(0x9800u + (17u * 32u) + 15u) = hud_t[0];
    *(volatile uint8_t *)(0x9800u + (17u * 32u) + 16u) = hud_t[1];
    *(volatile uint8_t *)(0x9C00u + (17u * 32u) + 15u) = hud_t[0];
    *(volatile uint8_t *)(0x9C00u + (17u * 32u) + 16u) = hud_t[1];
    hud_dirty = 0;
  }
  if (pend) {
    uint16_t off = pend_chunk ? CHUNK : 0;
    uint16_t src = (uint16_t)pend_src + off;
    uint16_t dst = 0x8000u + off;
    uint8_t l0 = LY_REG;
    VBK_REG = front ^ 1u;
    HDMA1_REG = (uint8_t)(src >> 8);
    HDMA2_REG = (uint8_t)src;
    HDMA3_REG = (uint8_t)(dst >> 8);
    HDMA4_REG = (uint8_t)dst;
    HDMA5_REG = (uint8_t)(CHUNK / 16 - 1);
#ifndef NO_DMA_PAD
    pad_n = CHUNK / 16;
    dma_pad();
#endif
    copy_lines += (uint8_t)(LY_REG - l0);
    copy_calls++;
    if (++pend_chunk == NCHUNK) {
      pend_chunk = 0;
      front ^= 1u;
      LCDC_REG ^= LCDCF_BG9C00;
      log_flip[pend_view & (LOGN - 1)] = vbl;
      nflips++;
      pend = 0;
    }
  }
}

static uint16_t read_vbl(void) {
  uint16_t v;
  do {
    v = vbl;
  } while (v != vbl);
  return v;
}

/* scanline timestamp: 154 per frame */
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

/* ---- maze ---- */
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
  return map[(uint16_t)y * MAPW + x];
}

static uint8_t dead[128]; /* bit per cell: open but on a dead-end branch */
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
  /* a few loops so it reads as rooms and halls, not a pure tree */
  for (k = 0; k < 28u; k++) {
    x = (uint8_t)(rnd() % 29u + 1u);
    y = (uint8_t)(rnd() % 29u + 1u);
    if (((x ^ y) & 1u) == 0) continue;
    if ((x & 1u) == 0 && !at(x - 1u, y) && !at(x + 1u, y)) map[(uint16_t)y * MAPW + x] = 0;
    if ((y & 1u) == 0 && !at(x, y - 1u) && !at(x, y + 1u)) map[(uint16_t)y * MAPW + x] = 0;
  }
  /* dead-end pruning: the walker keeps to the looped core, so the walk never stalls facing a dead end */
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
  /* doors: the end wall of most dead ends, and now and then a door in a hallway's side wall */
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
        /* a side wall of a straight hall */
        d = (uint8_t)(rnd() & 3u);
        if (at((uint8_t)(x + DX[d]), (uint8_t)(y + DY[d])) == 1u)
          map[(uint16_t)(y + DY[d]) * MAPW + (uint8_t)(x + DX[d])] = 2;
      }
    }
}

/* ---- right-hand walker over a ring of path cells ---- */
static uint8_t path_x[32], path_y[32];
static uint8_t sim_x = 1, sim_y = 1, sim_d = 0, path_n; /* path_n = cells generated so far (mod 256) */
static uint8_t wk; /* walker is between path[wk] and path[wk+1] */
static uint8_t wfrac;

/* seeded wander over the live (looped) part of the maze: mostly straight on, random turns at junctions,
   never back unless it has to */
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
  /* start on the first live cell */
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

/* the camera trails 3/8 cell behind the walker's point, so corners are seen from further back (no wall-filling
   frames while turning) */
#define TRAIL 96u
static uint16_t trail(uint16_t a) {
  uint16_t v = (uint16_t)(((uint16_t)T_COSABS[a & 1023u] * TRAIL) >> 8);
  return ((a & 1023u) < 256u || (a & 1023u) >= 768u) ? v : (uint16_t)-v;
}

static void walker_update(uint8_t dt) {
  if (cam_lock) return;
  while (dt--) walker_step();
  path_pos(wk, wfrac, &cam_x, &cam_y);
  cam_x -= trail(cam_a);
  cam_y -= trail((uint16_t)(cam_a - 256u));
  path_pos((uint8_t)(wk + AHEAD), wfrac, &spr_x, &spr_y);
  spr_on = 1;
}

/* ---- video setup ---- */
static const palette_color_t PALS[32] = {
#define WALLS RGB8(250, 204, 222), RGB8(178, 164, 230), RGB8(82, 78, 148)
    RGB8(232, 214, 244), WALLS, RGB8(214, 194, 232), WALLS, RGB8(190, 170, 214), WALLS,
    RGB8(160, 142, 192), WALLS, RGB8(146, 116, 156), WALLS, RGB8(178, 140, 176), WALLS,
    RGB8(206, 162, 192), WALLS, RGB8(232, 186, 206), WALLS,
};

static const char TITLE[] = "EGRESS.EXE";
static const char CHARS[] = "0123456789EGRSX.FP/ ";

static uint8_t char_tile(char c) {
  uint8_t i;
  for (i = 0; CHARS[i]; i++)
    if (CHARS[i] == c) return (uint8_t)(HUDT + i);
  return HUDT + 19u;
}

static uint8_t row_pal(uint8_t r) {
  if (r < TH / 2) return (uint8_t)(r * 4u / (TH / 2));
  return (uint8_t)(4u + (r - TH / 2) * 4u / (TH / 2));
}

static void video_init(void) {
  uint8_t x, y, t, a;
  uint8_t zero[16];
  for (x = 0; x < 16u; x++) zero[x] = 0;
  set_bkg_palette(0, 8, PALS);
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
        a = row_pal((uint8_t)(y - VY));
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
      *(uint8_t *)(0x9800u + o) = a;
      *(uint8_t *)(0x9C00u + o) = in ? (uint8_t)(a | 0x08u) : a;
    }
  VBK_REG = 0;
  SCX_REG = 0;
  SCY_REG = 0;
}

void main(void) {
  uint16_t t0, t1, t2, t3, t4, t5, lastv, v;
  uint8_t slot;
  DISPLAY_OFF;
  cpu_fast();
  buf[0] = (uint8_t *)(((uint16_t)bufmem + 15u) & 0xFFF0u);
  buf[1] = buf[0] + BUFSZ;
  gen_maze();
  walker_init();
  video_init();
  LCDC_REG = (uint8_t)((LCDC_REG | LCDCF_BG8000 | LCDCF_BGON) & (uint8_t)~LCDCF_BG9C00);
  front = 0;
  disable_interrupts();
  add_VBL(vbl_isr);
  set_interrupts(VBL_IFLAG);
  enable_interrupts();
  DISPLAY_ON;
  lastv = read_vbl();
  for (;;) {
    if (freeze) {
      frozen = 1;
      while (freeze) {
      }
      frozen = 0;
      lastv = read_vbl();
    }
    t0 = now();
    v = read_vbl();
    walker_update((uint8_t)((v - lastv) > 8u ? 8u : (v - lastv)));
    lastv = v;
    t1 = now();
    cast_all();
    t2 = now();
    fill_all(buf[cur]);
    t3 = now();
    sprite_draw(buf[cur]);
    t4 = now();
    while (pend) {
    }
    t5 = now();
    slot = (uint8_t)(nviews & (LOGN - 1));
    log_upd[slot] = t1 - t0;
    log_ray[slot] = t2 - t1;
    log_fill[slot] = t3 - t2;
    log_spr[slot] = t4 - t3;
    log_wait[slot] = t5 - t4;
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
