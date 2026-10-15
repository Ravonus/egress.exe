/* Raycaster core: portable C shared by the GB ROM and the host checker (host_check.c), so the cartridge's
   output can be compared byte for byte against a host build of the same code (SDCC miscompile guard). */
#include "ray.h"

uint8_t map[MAPW * MAPW];
uint16_t cam_x, cam_y, cam_a;
uint16_t spr_x, spr_y;
uint8_t spr_on;
uint8_t ray_h[NRAYS], ray_u[NRAYS], ray_t[NRAYS];
uint16_t ray_d[NRAYS];

uint8_t *run_p;
uint8_t run_n, run_lo0, run_hi0, run_lo1, run_hi1, run_par, run_keep;

/* quarter-square multiply: a*b = sq(a+b) - sq(|a-b|) */
static uint16_t mul8(uint8_t a, uint8_t b) {
  uint16_t s = (uint16_t)a + b;
  uint8_t d = a > b ? (uint8_t)(a - b) : (uint8_t)(b - a);
  return (uint16_t)(T_SQ[s] - T_SQ[d]);
}

/* (d * f) >> 8 for d 8.8 and f 0.8 */
static uint16_t mulhi(uint16_t d, uint8_t f) {
  uint16_t r = mul8((uint8_t)(d >> 8), f);
  r = (uint16_t)(r + (mul8((uint8_t)d, f) >> 8));
  return r;
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
      if (c) break;
    }
    perp = mulhi(d, T_COLCOS[i]);
    hi = perp >> 2;
    if (hi > 2047u) hi = 2047u;
    h = T_HEIGHT[hi];
    if (side == 0) {
      uint8_t off = (uint8_t)mulhi(d, T_COSABS[ay]);
      frac = ypos ? (uint8_t)(fy + off) : (uint8_t)(fy - off);
      u = frac >> 5;
      if (!xpos) u = (uint8_t)(7u - u);
    } else {
      uint8_t off = (uint8_t)mulhi(d, T_COSABS[a]);
      frac = xpos ? (uint8_t)(fx + off) : (uint8_t)(fx - off);
      u = frac >> 5;
      if (ypos) u = (uint8_t)(7u - u);
    }
    ray_h[i] = h;
    ray_u[i] = u;
    ray_t[i] = (uint8_t)(((c - 1u) << 1) | side);
    ray_d[i] = perp;
  }
}

#endif

#ifndef RUN_ASM
void run_set(void) {
  uint8_t *p = run_p;
  uint8_t n = run_n;
  do {
    p[0] = run_lo0;
    p[1] = run_hi0;
    p += 2;
  } while (--n);
}

void run_or(void) {
  uint8_t *p = run_p;
  uint8_t n = run_n;
  do {
    p[0] |= run_lo0;
    p[1] |= run_hi0;
    p += 2;
  } while (--n);
}

/* dithered: first row lo0/hi0, then each row toggles by xor lo1/hi1 */
void run_set_d(void) {
  uint8_t *p = run_p;
  uint8_t n = run_n, lo = run_lo0, hi = run_hi0;
  do {
    p[0] = lo;
    p[1] = hi;
    lo ^= run_lo1;
    hi ^= run_hi1;
    p += 2;
  } while (--n);
}

void run_or_d(void) {
  uint8_t *p = run_p;
  uint8_t n = run_n, lo = run_lo0, hi = run_hi0;
  do {
    p[0] |= lo;
    p[1] |= hi;
    lo ^= run_lo1;
    hi ^= run_hi1;
    p += 2;
  } while (--n);
}

void run_zero(void) {
  uint8_t *p = run_p;
  uint8_t n = run_n;
  do {
    p[0] = 0;
    p[1] = 0;
    p += 2;
  } while (--n);
}

/* p = (p & keep) | val */
void run_mask(void) {
  uint8_t *p = run_p;
  uint8_t n = run_n, k = run_keep;
  do {
    p[0] = (uint8_t)((p[0] & k) | run_lo0);
    p[1] = (uint8_t)((p[1] & k) | run_hi0);
    p += 2;
  } while (--n);
}
#endif

static const uint8_t SLOTMASK4[2] = {0xF0, 0x0F};
static const uint8_t SLOTMASK2[4] = {0xC0, 0x30, 0x0C, 0x03};

#define VOFF (255 - (VIEW_H + 255) / 2)
#define VEND (VOFF + VIEW_H)

#ifdef FILL_ASM
extern uint8_t *fill_buf;
void fill_asm(void);
void fill_all(uint8_t *buf) {
  fill_buf = buf;
  fill_asm();
}
#else
void fill_all(uint8_t *buf) {
  uint8_t i;
  uint8_t *strip = buf;
  for (i = 0; i < NRAYS; i++) {
    uint8_t slot = i & (RPT - 1);
    uint8_t m = (COLW == 4) ? SLOTMASK4[slot] : SLOTMASK2[slot];
    uint8_t h = ray_h[i];
    uint8_t vt = T_VTOP[h];
    const uint8_t *hv = &T_HV[(uint16_t)h << 4];
    const uint8_t *rn = &T_RUNS[(uint16_t)(uint8_t)(ray_t[i] * 8u + ray_u[i]) << 5];
    uint8_t dith = h < (VIEW_H / 4);
    uint8_t r = vt, e, a, b, vend, c;
    if (slot == 0 && i) strip += STRIP;
    if (slot == 0 && vt > VOFF) {
      run_p = strip;
      run_n = (uint8_t)(vt - VOFF);
      run_zero();
    }
    do {
      vend = rn[0];
      c = rn[1];
      rn += 2;
      e = (uint8_t)(vt + hv[vend]);
      a = r < VOFF ? VOFF : r;
      b = e > VEND ? VEND : e;
      if (b > a) {
        uint8_t lo = (c & 1u) ? m : 0, hi = (c & 2u) ? m : 0;
        run_p = strip + ((uint8_t)(a - VOFF) << 1);
        run_n = (uint8_t)(b - a);
        if (dith) {
          uint8_t pm = ((a - VOFF) & 1u) ? 0x55u : 0xAAu;
          run_lo0 = lo & pm;
          run_hi0 = hi & pm;
          run_lo1 = lo;
          run_hi1 = hi;
          if (slot) run_or_d();
          else run_set_d();
        } else {
          run_lo0 = lo;
          run_hi0 = hi;
          if (slot) run_or();
          else run_set();
        }
      }
      r = e;
    } while (vend != 15u);
    if (slot == 0 && r < VEND) {
      run_p = strip + ((uint8_t)(r - VOFF) << 1);
      run_n = (uint8_t)(VEND - r);
      run_zero();
    }
  }
}

#endif

static int16_t scos(uint16_t a) {
  int16_t v = T_COSABS[a & 1023u];
  return (a & 1023u) < 256u || (a & 1023u) >= 768u ? v : (int16_t)-v;
}

/* (r * c) >> 8 in sign-magnitude, |c| <= 255 */
static int16_t smulhi(int16_t r, int16_t c) {
  uint16_t ar = r < 0 ? (uint16_t)-r : (uint16_t)r;
  uint8_t ac = (uint8_t)(c < 0 ? -c : c);
  int16_t v = (int16_t)mulhi(ar, ac);
  return ((r < 0) != (c < 0)) ? (int16_t)-v : v;
}

void sprite_draw(uint8_t *buf) {
  int16_t rx, ry, depth, lat, off;
  int16_t cs, sn, cc, w, c0, c;
  uint8_t h, vt, ustep;
  uint16_t uacc;
  const uint8_t *hv;
  if (!spr_on) return;
  rx = (int16_t)(spr_x - cam_x);
  ry = (int16_t)(spr_y - cam_y);
  cs = scos(cam_a);
  sn = scos((uint16_t)(cam_a - 256u));
  depth = (int16_t)(smulhi(rx, cs) + smulhi(ry, sn));
  if (depth < 64) return;
  lat = (int16_t)(smulhi(ry, cs) - smulhi(rx, sn));
  h = T_HEIGHT[(depth >> 2) > 2047 ? 2047 : (depth >> 2)];
  /* screen offset in px = lateral (cells) * wall height at that depth (px per cell) */
  off = smulhi(lat, h);
  cc = (int16_t)(NRAYS / 2 + (off >> (COLW == 4 ? 2 : 1)));
  vt = T_VTOP[h];
  hv = &T_HV[(uint16_t)h << 4];
  w = (int16_t)(h / COLW);
  if (w < 1) w = 1;
  c0 = (int16_t)(cc - (w >> 1));
  ustep = (uint8_t)((16u * 16u) / (uint16_t)w > 255u ? 255u : (16u * 16u) / (uint16_t)w);
  uacc = 0;
  for (c = c0; c < c0 + w; c++, uacc = (uint16_t)(uacc + ustep)) {
    uint8_t u = (uint8_t)(uacc >> 4), slot, m, r, e, a, b, vend, x;
    const uint8_t *rn;
    uint8_t *strip;
    if (c < 0 || c >= NRAYS) continue;
    if ((uint16_t)depth >= ray_d[c]) continue;
    if (u > 15u) u = 15u;
    slot = (uint8_t)c & (RPT - 1);
    m = (COLW == 4) ? SLOTMASK4[slot] : SLOTMASK2[slot];
    strip = buf + (uint16_t)((uint8_t)c / RPT) * STRIP;
    rn = &T_SRUNS[(uint16_t)u << 5];
    r = vt;
    do {
      vend = rn[0];
      x = rn[1];
      rn += 2;
      e = (uint8_t)(vt + hv[vend]);
      if (x) {
        a = r < VOFF ? VOFF : r;
        b = e > VEND ? VEND : e;
        if (b > a) {
          run_p = strip + ((uint8_t)(a - VOFF) << 1);
          run_n = (uint8_t)(b - a);
          run_keep = (uint8_t)~m;
          run_lo0 = (x & 1u) ? m : 0;
          run_hi0 = (x & 2u) ? m : 0;
          run_mask();
        }
      }
      r = e;
    } while (vend != 15u);
  }
}
