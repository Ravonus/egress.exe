/* Raycaster v2 core (portable C reference; the GB build swaps the hot loops for asm with identical output). */
#include "ray2.h"
#include "tex_ids.h"

#ifndef __SDCC
uint8_t map[MAPW * MAPW];
#endif
uint16_t cam_x, cam_y, cam_a;
uint8_t ray_h[NRAYS], ray_u[NRAYS], ray_t[NRAYS];
uint16_t ray_d[NRAYS];
uint8_t ray_fv[NRAYS], ray_cv[NRAYS];

static uint16_t mulhi(uint16_t d, uint8_t f) {
  uint16_t r = (uint16_t)((uint16_t)(uint8_t)(d >> 8) * f);
  r = (uint16_t)(r + (((uint16_t)(uint8_t)d * f) >> 8));
  return r;
}

int16_t scos(uint16_t a) {
  int16_t v = T_COSABS[a & 1023u];
  return (a & 1023u) < 256u || (a & 1023u) >= 768u ? v : (int16_t)-v;
}

int16_t smulhi(int16_t r, int16_t c) {
  uint16_t ar = r < 0 ? (uint16_t)-r : (uint16_t)r;
  uint8_t ac = (uint8_t)(c < 0 ? -c : c);
  int16_t v = (int16_t)mulhi(ar, ac);
  return ((r < 0) != (c < 0)) ? (int16_t)-v : v;
}

#ifdef CAST_ASM
void cast_asm(void);
void cast_all(void) {
  cast_asm();
}
#else
void cast_all(void) {
  uint8_t i;
  uint8_t fx = (uint8_t)cam_x, fy = (uint8_t)cam_y;
  uint16_t base = (uint16_t)((uint16_t)(cam_y >> 8) * MAPW + (cam_x >> 8));
  for (i = 0; i < NRAYS; i++) {
    uint16_t a = (uint16_t)((cam_a + T_COLOFF[i]) & 1023u);
    uint16_t ay = (uint16_t)((a - 256u) & 1023u);
    uint16_t ddx = T_DELTA[a], ddy = T_DELTA[ay];
    uint8_t xpos = (a < 256u || a >= 768u);
    uint8_t ypos = (a < 512u);
    int16_t stx = xpos ? 1 : -1;
    int16_t sty = ypos ? MAPW : -MAPW;
    uint8_t gx = xpos ? (uint8_t)(255u - fx) : fx;
    uint8_t gy = ypos ? (uint8_t)(255u - fy) : fy;
    uint16_t sdx = mulhi(ddx, gx), sdy = mulhi(ddy, gy);
    uint16_t idx = base, d;
    uint8_t side, c, frac, u, h;
    uint16_t perp, hi;
    for (;;) {
      if (sdx < sdy) {
        idx = (uint16_t)(idx + stx);
        d = sdx;
        sdx = (uint16_t)(sdx + ddx);
        side = 0;
      } else {
        idx = (uint16_t)(idx + sty);
        d = sdy;
        sdy = (uint16_t)(sdy + ddy);
        side = 1;
      }
      c = map[idx & (MAPW * MAPW - 1)];
      if (c < 0x80u) break; /* walls are 1..127; open cells are 0x80 | floor set */
    }
    perp = mulhi(d, T_COLCOS[i]);
    hi = perp >> 2;
    if (hi > 2047u) hi = 2047u;
    h = T_HEIGHT[hi];
    if (side == 0) {
      uint8_t off = (uint8_t)mulhi(d, T_COSABS[ay]);
      frac = ypos ? (uint8_t)(fy + off) : (uint8_t)(fy - off);
      u = frac >> 4;
      if (!xpos) u = (uint8_t)(15u - u);
    } else {
      uint8_t off = (uint8_t)mulhi(d, T_COSABS[a]);
      frac = xpos ? (uint8_t)(fx + off) : (uint8_t)(fx - off);
      u = frac >> 4;
      if (ypos) u = (uint8_t)(15u - u);
    }
    ray_h[i] = h;
    ray_u[i] = u;
    ray_t[i] = (uint8_t)(((c - 1u) << 1) | side);
    ray_d[i] = perp;
  }
}
#endif

#ifdef WALLS_ASM
extern uint8_t *w_buf;
void walls_asm(void);
void walls_all(uint8_t *buf) {
  w_buf = buf;
  walls_asm();
}
#else
/* y-faces are shaded: light -> mid, and the mid field is dithered 50% toward the row's base colour (KEEL-style
   ordered shading, so the face still shows its pattern and the row gradient through the dither) */
static const uint8_t SIDE0[4] = {0, 1, 2, 3};
static const uint8_t SIDE1[4] = {0, 2, 2, 3};
/* Walls, column-major. Slot 0 (left 4 px of a tile byte) stores its byte outright (right nibble 0); slot 1 keeps
   the left nibble and writes its own. Rows outside each wall are left to floors_all. Distance fog: an ordered
   4x4 dither from the texel colour to colour 0 (the row's haze), level by wall height, +3 on y-faces. */
void walls_all(uint8_t *buf) {
  uint8_t i;
  uint8_t *strip = buf;
  for (i = 0; i < NRAYS; i++) {
    uint8_t slot = i & 1u;
    uint8_t m = slot ? 0x0Fu : 0xF0u;
    uint8_t h = ray_h[i], t = ray_t[i];
    uint8_t vt = T_VTOP[h];
    uint8_t k = T_FOGK[h];
    const uint8_t *sm = (t & 1u) ? SIDE1 : SIDE0;
    const uint8_t *pat;
    const uint8_t *rn = &T_WRUNS[T_WOFF[(h < MIPH ? MIPN : 0u) + (uint16_t)(t >> 1) * 16u + ray_u[i]]];
    uint8_t r = vt, e, a, b, vend, c;
    uint16_t top, bot;
    if (k > 16u) k = 16u;
    pat = &T_FOGPAT[k * 4u];
    if (slot == 0 && i) strip += STRIP;
    top = vt;
    bot = (uint16_t)vt + h;
    ray_cv[i] = (uint8_t)(top <= VOFF ? VIEW_H : (VIEW_H - (top - VOFF)));
    ray_fv[i] = (uint8_t)(bot >= VEND ? VIEW_H : (bot - VOFF));
    do {
      vend = rn[0];
      c = rn[1];
      rn += 2;
      e = (uint8_t)(vt + HV2(vend, h));
      a = r < VOFF ? VOFF : r;
      b = e > VEND ? VEND : e;
      if (b > a) {
        uint8_t lo, hi;
        pat = &T_FOGPAT[(uint8_t)(((t & 1u) && c == 2u && k < 8u) ? 8u : k) * 4u];
        c = sm[c];
        lo = (c & 1u) ? m : 0;
        hi = (c & 2u) ? m : 0;
        uint8_t row = (uint8_t)(a - VOFF), n = (uint8_t)(b - a);
#ifdef STATS
        { extern unsigned st_runs, st_rows[2][4]; uint8_t md = ((lo | hi) == 0) ? 3 : (pat[0] == 0xFF && pat[1] == 0xFF) ? 0 : (pat[0] == 0x55 || pat[0] == 0xAA) ? 1 : 2; st_runs++; st_rows[slot][md] += n; }
#endif
        uint8_t *p = strip + ((uint16_t)row << 1);
        do {
          uint8_t pm = pat[row & 3u];
          if (slot) {
            p[0] = (uint8_t)((p[0] & 0xF0u) | (lo & pm));
            p[1] = (uint8_t)((p[1] & 0xF0u) | (hi & pm));
          } else {
            p[0] = (uint8_t)(lo & pm);
            p[1] = (uint8_t)(hi & pm);
          }
          p += 2;
          row++;
        } while (--n);
      }
#ifdef STATS
      { extern unsigned st_allruns; st_allruns++; }
#endif
      r = e;
    } while (vend != 15u);
  }
}
#endif

#ifdef FLOORS_ASM
extern uint8_t *w_buf;
extern int8_t f_lx, f_ly, f_ux, f_uy;
extern uint16_t f_cx8, f_cy8;
extern uint8_t f_minfv;
void floors_asm(void);
void floors_all(uint8_t *buf) {
  int16_t cs = scos(cam_a), sn = scos((uint16_t)(cam_a - 256u));
  uint8_t i, m = VIEW_H;
  f_lx = (int8_t)((cs >> 2) - (((int16_t)T_LK[0] * sn) >> 8));
  f_ly = (int8_t)((sn >> 2) + (((int16_t)T_LK[0] * cs) >> 8));
  f_ux = (int8_t)(((int16_t)T_LK[1] * -sn) >> 8);
  f_uy = (int8_t)(((int16_t)T_LK[1] * cs) >> 8);
  f_cx8 = (uint16_t)(cam_x << 3);
  f_cy8 = (uint16_t)(cam_y << 3);
  for (i = 0; i < TW; i++) {
    uint8_t f0 = ray_fv[2u * i], f1 = ray_fv[2u * i + 1u], c0 = ray_cv[2u * i], c1 = ray_cv[2u * i + 1u];
    uint8_t *rec = (uint8_t *)(0xCB00u + 4u * i);
    uint8_t fmin = f0 < f1 ? f0 : f1;
    rec[0] = f0;
    rec[1] = f1;
    rec[2] = c0;
    rec[3] = c1;
    *(volatile uint8_t *)(0xFFC8u + i) = fmin; /* FMIN */
    *(volatile uint8_t *)(0xFFD8u + i) = c0 > c1 ? c0 : c1; /* CMAX */
    if (fmin < m) m = fmin;
  }
  f_minfv = m;
  w_buf = buf;
  floors_asm();
}
#else
static const uint8_t NIB[4] = {0x00u, 0x0Fu, 0xF0u, 0xFFu}; /* [c0 bit | c1 bit] -> byte bits (slot0 = 0xF0) */

/* one floor/ceiling pixel row for a tile column: f = visibility bits (2 = slot 0, 1 = slot 1) */
static void put_row(uint8_t *p, uint8_t f, uint8_t c0, uint8_t c1, uint8_t pm) {
  uint8_t lo = (uint8_t)(NIB[((c0 & 1u) << 1) | (c1 & 1u)] & pm);
  uint8_t hi = (uint8_t)(NIB[(c0 & 2u) | ((c1 >> 1) & 1u)] & pm);
  uint8_t mk = NIB[f];
  if (f == 3u) {
    p[0] = lo;
    p[1] = hi;
  } else {
    p[0] = (uint8_t)((p[0] & (uint8_t)~mk) | (lo & mk));
    p[1] = (uint8_t)((p[1] & (uint8_t)~mk) | (hi & mk));
  }
}

/* Floor + ceiling, row-major (classic floor casting): every sample row has a fixed distance D; the world point
   for ray column i is cam + D * (L + i * U), stepped with 16-bit adds in 1/2048-cell units (so the 32-cell map
   wraps on its own: cell = hi byte >> 3, texel = hi byte & 15 across a 2x2-cell block). The ceiling uses the
   mirrored row and the same world point. */
void floors_all(uint8_t *buf) {
  int16_t cs = scos(cam_a), sn = scos((uint16_t)(cam_a - 256u));
  int16_t lx = (int16_t)((cs >> 2) - (((int16_t)T_LK[0] * sn) >> 8));
  int16_t ly = (int16_t)((sn >> 2) + (((int16_t)T_LK[0] * cs) >> 8));
  int16_t ux = (int16_t)(((int16_t)T_LK[1] * -sn) >> 8);
  int16_t uy = (int16_t)(((int16_t)T_LK[1] * cs) >> 8);
  uint16_t cx8 = (uint16_t)(cam_x << 3), cy8 = (uint16_t)(cam_y << 3);
  uint8_t j, minfv = VIEW_H, i;
  for (i = 0; i < NRAYS; i++)
    if (ray_fv[i] < minfv) minfv = ray_fv[i];
  for (j = 0; j < NFROW; j++) {
    uint8_t y0 = (uint8_t)(VIEW_H / 2 + j * FR);
    uint8_t d = T_FD[j], t, k;
    uint16_t px, py, sx, sy;
    if ((uint8_t)(y0 + FR - 1u) < minfv) continue;
    px = (uint16_t)(cx8 + ((uint16_t)((int16_t)d * lx) << 1));
    py = (uint16_t)(cy8 + ((uint16_t)((int16_t)d * ly) << 1));
    sx = (uint16_t)(((int16_t)d * ux) >> 4);
    sy = (uint16_t)(((int16_t)d * uy) >> 4);
    for (t = 0; t < TW; t++) {
      uint8_t i0 = (uint8_t)(t * 2u), b0 = 0, b1 = 0;
      uint8_t *col = buf + (uint16_t)t * STRIP;
      uint8_t vis0 = (uint8_t)(y0 + FR - 1u) >= ray_fv[i0];
      uint8_t vis1 = (uint8_t)(y0 + FR - 1u) >= ray_fv[i0 + 1u];
      if (d) {
        if (vis0) {
          uint8_t hx = (uint8_t)(px >> 8), hy = (uint8_t)(py >> 8);
          uint8_t s = map[((uint16_t)(hy >> 3) << 5) | (hx >> 3)] & 7u;
          b0 = T_FTEX[((uint16_t)s << 8) | (uint8_t)((hy & 15u) << 4) | (hx & 15u)];
        }
        px = (uint16_t)(px + sx);
        py = (uint16_t)(py + sy);
        if (vis1) {
          uint8_t hx = (uint8_t)(px >> 8), hy = (uint8_t)(py >> 8);
          uint8_t s = map[((uint16_t)(hy >> 3) << 5) | (hx >> 3)] & 7u;
          b1 = T_FTEX[((uint16_t)s << 8) | (uint8_t)((hy & 15u) << 4) | (hx & 15u)];
        }
        px = (uint16_t)(px + sx);
        py = (uint16_t)(py + sy);
      }
      if (!(vis0 | vis1)) continue;
      for (k = 0; k < FR; k++) {
        uint8_t y = (uint8_t)(y0 + k), yc = (uint8_t)(VIEW_H - 1u - y);
        uint8_t f = (uint8_t)(((y >= ray_fv[i0]) ? 2u : 0u) | ((y >= ray_fv[i0 + 1u]) ? 1u : 0u));
        uint8_t g = (uint8_t)(((y >= ray_cv[i0]) ? 2u : 0u) | ((y >= ray_cv[i0 + 1u]) ? 1u : 0u));
        if (f) put_row(col + ((uint16_t)y << 1), f, b0, b1, T_FPAT[y]);
        if (g) put_row(col + ((uint16_t)yc << 1), g, (uint8_t)(b0 >> 2), (uint8_t)(b1 >> 2), T_FPAT[yc]);
      }
    }
  }
}
#endif
