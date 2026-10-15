/* OBJ billboards for the v2 raycaster (see obj2.h). Linked right after main2.o so the VBlank half (upload, flip)
   sits in ROM0: the ISR can fire while the fill asm has a data bank mapped at 0x4000. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include "ray2.h"
#include "obj2.h"
#include "bb_data.h"
#ifndef SPRCUT
#define SPRCUT 0
#endif

#if FS
/* fullscreen: staging (32 sprites) and the OAM buffer live in each view's own WRAM bank (2 / 3), next to its
   framebuffer, so a view can build its billboards while the previous one is still being copied */
#define VX0 0
#define VY0 0
#define VIEW_SH (VIEW_H * 2)
#define HSCALE(h) ((h) > 127u ? 255u : (uint8_t)((h) << 1)) /* rows are shown twice: screen px = 2 rows */
#define STAGE ((uint8_t *)0xDA00u)
#define STAGE_MAX 28u
#define OAMBUF(i) ((uint8_t *)0xDE00u)
#define OBJT0 160u
#define FIXB 0x08u /* the fixed OBJ tiles (crown, spark, weapon) live in VRAM bank 1 */
#define WPN_X(f) ((f) ? 96u : 120u) /* weapon (24x48), idle low right, strike swung in */
#define WPN_Y(f) ((f) ? 72u : 80u)
#else
#define VX0 32 /* view origin obj_on screen (px) */
#define VY0 40
#define VIEW_SH VIEW_H
#define HSCALE(h) (h)
#define STAGE ((uint8_t *)0xC800u) /* 48 sprites x 32 bytes, below REC (0xCB00) */
#define STAGE_MAX 24u              /* 8x16 sprites per view (48 tiles) */
#define OAMBUF(i) ((uint8_t *)(0xDC00u + ((uint16_t)(i) << 8)))
#define OBJT0 96u /* first OBJ tile of the staging upload */
#define WPN_X(f) ((f) ? 56u : 72u)
#define WPN_Y(f) ((f) ? 64u : 74u)
#define FIXB 0u
#endif

mob_t mob[NMOB];
uint8_t weapon_frame;
uint8_t obj_count;

uint8_t obj_stage_n;
uint8_t objn[2], built[2]; /* read by vbl_fs (lcd2.s) */
static uint8_t lastn[2];
uint16_t objpal[2][32];
static uint8_t tick;
static const uint8_t *BB_DROW[BB_NVAR * BB_NFRAME];
uint8_t vpad[NRAYS + 6]; /* vpad[j + 2] = ray j shows this mob (0x0F), zero padding both sides */
static uint8_t pkey[NMOB] = {0xFF, 0xFF, 0xFF};
static uint16_t pcache[NMOB][3];

/* bb_col (fill2.s): one billboard column of 8x16 sprites from a ROM bank into the staging area and OAM, rows
   clipped to the view and ANDed with the column's occlusion mask */
extern uint16_t bbc_src;
extern uint8_t bbc_mask, bbc_bank;
void oam_dma(uint8_t page);
extern uint8_t bbe_y8, bbe_nr, bbe_ox, bbe_attr;
void bb_col(void);
extern uint8_t vf_j0, vf_n;
extern uint16_t vf_d;
void vis_fill_asm(void);
/* vpad[j + 2] = d < ray_d[j] ? 0x0F : 0 for n rays from j0 (d 0xFFFF clears) */
#define vis_fill(j, n, dd) do { vf_j0 = (j); vf_n = (n); vf_d = (dd); vis_fill_asm(); } while (0)

extern uint16_t pal_cur[32];
extern uint8_t front;

/* a + (b - a) * q / 4 per channel, q 0..3 (adds and shifts only: no multiplies in the view loop) */
static uint8_t mixc(uint8_t ca, uint8_t cb, uint8_t q) {
  if (q == 1u) return (uint8_t)((ca + ca + ca + cb) >> 2);
  if (q == 2u) return (uint8_t)((ca + cb) >> 1);
  return (uint8_t)((ca + cb + cb + cb) >> 2);
}
static uint16_t mix4(uint16_t a, uint16_t b, uint8_t q) {
  uint8_t r = mixc((uint8_t)a & 31u, (uint8_t)b & 31u, q);
  uint8_t g = mixc((uint8_t)(a >> 5) & 31u, (uint8_t)(b >> 5) & 31u, q);
  uint8_t bl = mixc((uint8_t)(a >> 10) & 31u, (uint8_t)(b >> 10) & 31u, q);
  return (uint16_t)r | ((uint16_t)g << 5) | ((uint16_t)bl << 10);
}

static const uint16_t WEAPON_PAL[4] = {0, RGB8(236, 252, 255), RGB8(150, 160, 214), RGB8(52, 44, 96)};
static const uint16_t CROWN_PAL[4][3] = {{RGB8(255, 236, 140), RGB8(255, 160, 60), RGB8(120, 40, 60)},
                                         {RGB8(255, 250, 200), RGB8(255, 190, 90), RGB8(140, 50, 70)},
                                         {RGB8(255, 214, 250), RGB8(255, 120, 200), RGB8(110, 30, 90)},
                                         {RGB8(255, 250, 200), RGB8(255, 190, 90), RGB8(140, 50, 70)}};
static const int8_t ORBIT[8][2] = {{10, -2}, {7, -8}, {0, -11}, {-7, -8}, {-10, -2}, {-7, 4}, {0, 6}, {7, 4}};

void obj_init(void) {
  uint8_t i, k;
  _shadow_OAM_base = 0; /* no automatic OAM DMA: the flip copies the view's own OAM buffer */
#if FS
  for (k = 2; k < 4u; k++) {
    SVBK_REG = k;
    for (i = 0; i < 160u; i++) OAMBUF(0)[i] = 0;
  }
  SVBK_REG = 1;
#else
  for (i = 0; i < 160u; i++) {
    OAMBUF(0)[i] = 0;
    OAMBUF(1)[i] = 0;
  }
#endif
  VBK_REG = FIXB ? 1u : 0u;
  set_sprite_data(T_CROWN, BB_NFIXED, BB_FIXED);
  VBK_REG = 0;
  LCDC_REG |= LCDCF_OBJ16 | LCDCF_OBJON;
  weapon_frame = 0;
  built[0] = built[1] = 0;
  for (i = 0; i < BB_NVAR * BB_NFRAME; i++) BB_DROW[i] = &BB_DESC[(uint16_t)i * (BB_NBUCKET * 7u)];
  for (i = 0; i < NRAYS + 6u; i++) vpad[i] = 0;
  for (i = 0; i < 4u; i++) objpal[0][6u * 4u + i] = objpal[1][6u * 4u + i] = WEAPON_PAL[i];
#if FS
  set_sprite_palette(6, 1, WEAPON_PAL); /* fixed: the flip only rewrites the mob (0-2) and crown (7) palettes */
#endif
}

uint8_t *obj_oam;
uint8_t obj_on;

static void put(uint8_t y, uint8_t x, uint8_t t, uint8_t a) {
  if (obj_on >= 40u) return;
  obj_oam[0] = y;
  obj_oam[1] = x;
  obj_oam[2] = t;
  obj_oam[3] = a;
  obj_oam += 4;
  obj_on++;
}

#if FS
/* the CGB shows at most 10 OBJ per line: OAM is filled nearest first, and an entry is only placed when every
   8-line band it touches has room (bands make it conservative: an edge band can count two sprites that never
   share a line), so a crowd loses its farthest columns whole instead of the hardware slicing them per line */
#define NBAND (VIEW_SH / 8u)
uint8_t band[NBAND], bb0, bb1, band_k;
uint8_t band_max(uint8_t b0, uint8_t n); /* fill2.s: max of band[b0 .. b0 + n - 1] */
void band_add(uint8_t b0, uint8_t n); /* fill2.s: band[b0 .. b0 + n - 1] += band_k */
/* room = OBJ that still fit on every line of [y0, y1) (10 - the fullest band); 10 when outside the view */
static uint8_t span_room(int16_t y0, int16_t y1) {
  if (y0 < 0) y0 = 0;
  if (y1 > (int16_t)VIEW_SH) y1 = VIEW_SH;
  if (y1 <= y0) { bb1 = 0; return 10u; }
  bb0 = (uint8_t)((uint8_t)y0 >> 3);
  bb1 = (uint8_t)((uint8_t)((uint8_t)(y1 - 1) >> 3) - bb0 + 1u);
  return (uint8_t)(10u - band_max(bb0, bb1));
}
/* add n OBJ to the bands of the last span_room */
#define span_add(n) do { if (bb1) { band_k = (n); band_add(bb0, bb1); } } while (0)
static uint8_t span_ok(int16_t y0, int16_t y1) {
  if (!span_room(y0, y1)) return 0;
  span_add(1u);
  return 1;
}
#else
#define span_ok(y0, y1) 1
#define span_room(y0, y1) 10u
#define span_add(n)
#endif

/* per-ray visibility nibbles for one depth (0x0F = this 4 px column shows the mob) */
static uint8_t pj0, pj1;
#define nib(j) vpad[(uint8_t)((j) + 2)]

void obj_build(uint8_t cur) {
  int16_t cs = scos(cam_a), sn = scos((uint16_t)(cam_a - 256u));
  /* this view's tiles land in the VRAM bank its own flip will show: views alternate banks, view cur -> cur ^ 1 */
  uint8_t order[NMOB], n = 0, i, k, bank = (uint8_t)(cur ^ 1u) << 3;
  int16_t dep[NMOB];
  uint16_t *pal = objpal[cur];
  uint16_t haze = pal_cur[3u * 4u];
  tick++;
  obj_oam = OAMBUF(cur);
  obj_on = 0;
  obj_stage_n = 0;
#if FS
  for (i = 0; i < NBAND; i++) band[i] = 0;
#endif
  /* the weapon, in front of everything */
  if (weapon_frame != 0xFFu) {
    uint8_t wx = (uint8_t)(WPN_X(weapon_frame) + 8u), wy = (uint8_t)(WPN_Y(weapon_frame) + 16u);
    uint8_t t = (uint8_t)(T_WEAPON + weapon_frame * (BB_WC * BB_WR * 2u)), c, r;
    span_room((int16_t)WPN_Y(weapon_frame), (int16_t)(WPN_Y(weapon_frame) + BB_WR * 16u));
    span_add(BB_WC);
    for (c = 0; c < BB_WC; c++)
      for (r = 0; r < BB_WR; r++, t += 2u) put((uint8_t)(wy + r * 16u), (uint8_t)(wx + c * 8u), t, 6u | FIXB);
  }
  for (i = 0; i < 3u; i++) pal[7u * 4u + 1u + i] = CROWN_PAL[(tick >> 2) & 3u][i];
#if SPRCUT == 1
  goto fin;
#endif
  /* depth sort, nearest first (OAM order = priority) */
  for (i = 0; i < NMOB; i++) {
    int16_t rx, ry;
    if (!mob[i].on) continue;
    rx = (int16_t)(mob[i].x - cam_x);
    ry = (int16_t)(mob[i].y - cam_y);
    dep[i] = (int16_t)(smulhi(rx, cs) + smulhi(ry, sn));
    if (dep[i] < 80) continue;
    for (k = n; k && dep[order[k - 1u]] > dep[i]; k--) order[k] = order[k - 1u];
    order[k] = i;
    n++;
  }
  for (k = 0; k < n; k++) {
    mob_t *m = &mob[order[k]];
    uint8_t mi = order[k];
    int16_t rx = (int16_t)(m->x - cam_x), ry = (int16_t)(m->y - cam_y);
    int16_t d = dep[mi], lat = (int16_t)(smulhi(ry, cs) - smulhi(rx, sn));
    uint16_t di = (uint16_t)d >> 2;
    uint8_t hr = T_HEIGHT[di > 2047u ? 2047u : di], h = HSCALE(hr), hb = (uint8_t)((h >> 1) + (h >> 2) + (h >> 4)), b;
    int16_t cx = (int16_t)(VIEW_W / 2 + smulhi(lat, h)), bottom = (int16_t)((VIEW_SH + h) >> 1), left, top;
    const uint8_t *ds;
    uint8_t nc, nr, c, j0, j1, room, placed;
    uint16_t addr;
    if (hb < 9u) continue;
    for (b = BB_NBUCKET - 1u; b && BB_BUCKET[b] > hb; b--) {
    }
    ds = BB_DROW[(uint8_t)(m->var * BB_NFRAME + m->frame)] + (uint8_t)((b << 3) - b);
    nc = ds[3];
    nr = ds[4];
    addr = (uint16_t)(ds[1] | ((uint16_t)ds[2] << 8));
    left = (int16_t)(cx - (BB_BUCKET[b] >> 1) + ds[5]);
    top = (int16_t)(bottom - BB_BUCKET[b] + (int8_t)ds[6]);
    if (left + (int16_t)nc * 8 <= 0 || left >= VIEW_W || top >= VIEW_SH) continue;
#if SPRCUT == 2
    continue;
#endif
    /* visibility of the 4 px columns this billboard covers */
    j0 = left < 0 ? 0u : (uint8_t)(left >> 2);
    j1 = (uint8_t)(((left + (int16_t)nc * 8 + 3) >> 2) > NRAYS ? NRAYS : ((left + (int16_t)nc * 8 + 3) >> 2));
    if (pj1 > pj0) vis_fill(pj0, (uint8_t)(pj1 - pj0), 0xFFFFu);
    pj0 = j0;
    pj1 = j1;
    if (j1 > j0) vis_fill(j0, (uint8_t)(j1 - j0), (uint16_t)d);
    /* palette: the base colours hazed by distance (4 steps, and further as a death fade), cached per mob until the
       step changes; the tell and hit flashes are fixed colours, never hazed, so a wind-up reads at any distance */
    {
      uint8_t q = (uint8_t)(T_FOGK[hr] >> 2), fx = m->fx;
      uint16_t *pp = &pal[mi * 4u + 1u];
      if (fx == FX_DEAD) q = (uint8_t)(q + (m->fxt >> 2));
      if (q > 3u) q = 3u;
      if (fx == FX_TELL && !(tick & 2u)) fx = FX_NONE;
      if (fx == FX_HIT) {
        pp[0] = pp[1] = RGB8(255, 255, 255);
        pp[2] = RGB8(120, 60, 90);
      } else if (fx == FX_TELL) {
        pp[0] = RGB8(255, 255, 220);
        pp[1] = RGB8(255, 214, 90);
        pp[2] = pcache[mi][2];
      } else {
        uint8_t key = (uint8_t)((q << 4) | m->var);
        if (key != pkey[mi]) {
          const uint16_t *bp = &BB_PAL[m->var * 3u];
          pkey[mi] = key;
          for (c = 0; c < 3u; c++) pcache[mi][c] = q ? mix4(bp[c], haze, q) : bp[c];
        }
        pp[0] = pcache[mi][0];
        pp[1] = pcache[mi][1];
        pp[2] = pcache[mi][2];
      }
    }
#if SPRCUT == 3
    continue;
#endif
    room = span_room(top, (int16_t)(top + (int16_t)nr * 16));
    placed = 0;
    for (c = 0; c < nc; c++) {
      int16_t x = left + (int16_t)c * 8;
      uint8_t mask;
      if (x <= -8 || x >= VIEW_W) {
        addr += (uint16_t)nr * 32u;
        continue;
      }
      {
        uint8_t xp = (uint8_t)(x + 8), j = (uint8_t)(xp >> 2), sh = (uint8_t)(4u - (xp & 3u));
        uint16_t w = ((uint16_t)vpad[j] << 8) | (uint8_t)((vpad[j + 1u] << 4) | vpad[j + 2u]);
        mask = (uint8_t)(w >> sh);
      }
      if (mask && placed < room) {
        placed++;
        bbc_src = addr;
        bbc_bank = ds[0];
        bbc_mask = mask;
        bbe_y8 = (uint8_t)(top + 64);
        bbe_nr = nr;
        bbe_ox = (uint8_t)(VX0 + 8 + x);
        bbe_attr = (uint8_t)(bank | mi);
#ifndef NOBBCOL
        bb_col();
#endif
      }
      addr += (uint16_t)nr * 32u;
    }
    span_room(top, (int16_t)(top + (int16_t)nr * 16));
    span_add(placed);
    /* the elite: a crown over its head and two orbiting sparks (hidden with the body behind walls) */
    if (m->var == 2u && m->fx != FX_DEAD) {
      int16_t hx = cx - 4, hy = top - 6;
      uint8_t o = (uint8_t)((tick >> 1) & 7u);
      int8_t jj = (int8_t)(cx >> 2);
      if (cx >= 0 && cx < VIEW_W && nib(jj) && hy > -8 && hy < VIEW_SH && span_ok(hy, (int16_t)(hy + 16)))
        put((uint8_t)(VY0 + 16 + hy), (uint8_t)(VX0 + 8 + hx), T_CROWN, 7u | FIXB);
      for (c = 0; c < 2u; c++) {
        int8_t ox = ORBIT[(o + c * 4u) & 7u][0], oy = ORBIT[(o + c * 4u) & 7u][1];
        int16_t sx, sy;
        if (b < 2u) { ox >>= 1; oy >>= 1; }
        else if (b > 3u) { ox = (int8_t)(ox + (ox >> 1)); oy = (int8_t)(oy + (oy >> 1)); }
        sx = cx + ox - 4;
        sy = top + (int16_t)(BB_BUCKET[b] >> 1) + oy;
        if (sx < 0 || sx >= VIEW_W - 8 || sy < 0 || sy >= VIEW_SH - 8 || !nib((int8_t)((sx + 4) >> 2)) ||
            !span_ok(sy, (int16_t)(sy + 16)))
          continue;
        put((uint8_t)(VY0 + 16 + sy), (uint8_t)(VX0 + 8 + sx), T_SPARK, 7u | FIXB);
      }
    }
  }
#if SPRCUT == 1
fin:
#endif
  if (pj1 > pj0) vis_fill(pj0, (uint8_t)(pj1 - pj0), 0xFFFFu);
  pj0 = pj1 = 0;
  obj_count = obj_on;
  /* hide only the entries the previous view in this buffer used */
  for (i = obj_on; i < lastn[cur]; i++, obj_oam += 4) obj_oam[0] = 0;
  lastn[cur] = obj_on;
  objn[cur] = obj_stage_n;
  built[cur] = 1;
}

/* ISR, second VBlank of a flip: this view's billboard tiles into the hidden bank (VBK already set) */
void obj_vbl_upload(uint8_t b) {
  uint8_t n = objn[b];
  if (!n) return;
  HDMA1_REG = (uint8_t)((uint16_t)STAGE >> 8);
  HDMA2_REG = 0;
  HDMA3_REG = (uint8_t)(0x8000u + OBJT0 * 16u >> 8);
  HDMA4_REG = (uint8_t)(0x8000u + OBJT0 * 16u);
  HDMA5_REG = (uint8_t)(n * 2u - 1u);
}

/* ISR, at the flip: OAM and OBJ palettes of the view that just became visible */
void obj_vbl_flip(uint8_t b) {
  uint8_t i;
  const uint16_t *p;
  if (!built[b]) return;
  oam_dma((uint8_t)((uint16_t)OAMBUF(b) >> 8));
  p = objpal[b];
  OCPS_REG = 0x80u;
  for (i = 0; i < 32u; i++) {
    OCPD_REG = (uint8_t)p[i];
    OCPD_REG = (uint8_t)(p[i] >> 8);
  }
}
