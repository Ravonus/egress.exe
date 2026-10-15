/* Raycaster prototype: shared declarations (GB build and host checker). */
#ifndef RAY_H
#define RAY_H
#include <stdint.h>

#ifndef VIEW_W
#define VIEW_W 128
#endif
#ifndef VIEW_H
#define VIEW_H 80
#endif
#ifndef COLW
#define COLW 4 /* screen pixels per ray column: 4 or 2 */
#endif

#define NRAYS (VIEW_W / COLW)
#define TW (VIEW_W / 8)
#define TH (VIEW_H / 8)
#define RPT (8 / COLW) /* rays per tile column */
#define STRIP (VIEW_H * 2) /* bytes per tile column (tiles stored column-major) */
#define BUFSZ (TW * STRIP)
#define MAPW 32

extern const uint8_t T_COSABS[1024];
extern const uint16_t T_DELTA[1024];
extern const uint16_t T_COLOFF[NRAYS];
extern const uint8_t T_COLCOS[NRAYS];
extern const uint8_t T_HEIGHT[2048];
extern const uint8_t T_TEX[512];
extern const uint8_t T_SPRTEX[256];
extern const uint8_t T_HUDTILES[];
extern const uint16_t T_SPRPROJ;
extern const uint8_t T_VTOP[256];
extern const uint8_t T_HV[4096];
extern const uint8_t T_RUNS[1024];
extern const uint8_t T_SRUNS[512];
extern const uint16_t T_SQ[512];

extern uint8_t map[MAPW * MAPW];
extern uint16_t cam_x, cam_y, cam_a; /* 8.8 cells, angle 0..1023 */
extern uint16_t spr_x, spr_y;
extern uint8_t spr_on;

extern uint8_t ray_h[NRAYS], ray_u[NRAYS], ray_t[NRAYS];
extern uint16_t ray_d[NRAYS];

void cast_all(void);
void fill_all(uint8_t *buf);
void sprite_draw(uint8_t *buf);

/* run writer interface (C or asm): rows [r0, r0+n) of a strip */
extern uint8_t *run_p;
extern uint8_t run_n, run_lo0, run_hi0, run_lo1, run_hi1, run_par, run_keep;
void run_set(void);
void run_or(void);
void run_mask(void);
void run_set_d(void);
void run_or_d(void);
void run_zero(void);

#endif
