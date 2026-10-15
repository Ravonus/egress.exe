/* Raycaster v2: textured, dithered walls + cast floor/ceiling + OBJ billboards. Shared by the GB ROM and the host
   checker (host2.c), so the cartridge's BG framebuffer can be diffed against the host build of the same C. */
#ifndef RAY2_H
#define RAY2_H
#include <stdint.h>

#ifndef VIEW_W
#define VIEW_W 96
#endif
#ifndef VIEW_H
#define VIEW_H 64
#endif
#define COLW 4
#ifndef FR
#define FR 2 /* floor/ceiling texel height in pixel rows */
#endif

#define NRAYS (VIEW_W / COLW)
#define TW (VIEW_W / 8)
#define TH (VIEW_H / 8)
#define STRIP (VIEW_H * 2)
#define BUFSZ (TW * STRIP)
#define MAPW 32
#define VOFF (255 - (VIEW_H + 255) / 2)
#define VEND (VOFF + VIEW_H)

/* generated (tables2.c) */
extern const uint8_t T_COSABS[1024];
extern const uint16_t T_DELTA[1024];
extern const uint16_t T_COLOFF[NRAYS];
extern const uint8_t T_COLCOS[NRAYS];
extern const uint8_t T_HEIGHT[2048];
extern const uint8_t T_VTOP[256];
extern const uint8_t T_FOGK[256];    /* wall fog level 0..16 by wall height */
extern const uint8_t T_FOGPAT[17 * 4]; /* 4-bit Bayer keep-pattern in both nibbles, [k][row & 3] */
extern const uint8_t T_FD[];   /* per floor row group: distance, 4.4 cells (0 = all haze) */
extern const uint8_t T_FY0[];  /* group's first pixel row */
extern const uint8_t T_FN[];   /* group's pixel rows (FR near, more toward the horizon) */
extern const uint8_t T_FPAT[VIEW_H]; /* floor/ceiling fog pattern per pixel row */
extern const uint16_t T_WOFF[];      /* wall runs offset, [tex][u] */
extern const uint8_t T_WRUNS[];      /* (vend, colour) pairs, a column ends at vend 15 */
extern const uint8_t T_FTEX[];       /* floor/ceiling sets: [set][v][u]: floor bits 0-1, ceiling bits 2-3 */
extern const int8_t T_LK[2];         /* X0 * 64 and U * 2048 (see gen2.py) */
#ifdef __SDCC
#define HV2(v, h) (*(const uint8_t *)(0x6800u + ((uint16_t)(v) << 8) + (uint8_t)(h)))
#else
extern const uint8_t T_HV2[16 * 256];
#define HV2(v, h) (T_HV2[((uint16_t)(v) << 8) + (uint8_t)(h)])
#endif

/* walls 1..127 (texture id), open cells 0x80 | floor/ceiling set; on the cartridge it sits at 0xD800 (1 KB aligned,
   so the floor asm builds cell addresses from bits; the asm defines _map there) */
#ifdef __SDCC
#define map ((uint8_t *)0xD800u)
#else
extern uint8_t map[MAPW * MAPW];
#endif
extern uint16_t cam_x, cam_y, cam_a; /* 8.8 cells, angle 0..1023 */

extern uint8_t ray_h[NRAYS], ray_u[NRAYS], ray_t[NRAYS];
extern uint16_t ray_d[NRAYS];
extern uint8_t ray_fv[NRAYS], ray_cv[NRAYS]; /* first floor row, first row whose ceiling mirror shows */

void cast_all(void);
void walls_all(uint8_t *buf);
void floors_all(uint8_t *buf);
int16_t scos(uint16_t a);
int16_t smulhi(int16_t r, int16_t c);

#endif
