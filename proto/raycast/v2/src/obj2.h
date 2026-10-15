/* OBJ billboards (v2): pre-scaled mob frames (gen_bb.py, ROM banks 5+) copied per view into a WRAM staging area
   with 4 px column occlusion against the wall depths, uploaded by GDMA in the second VBlank into the hidden VRAM
   bank, and shown with the BG flip (double-buffered OAM + OBJ palettes). Plus the first-person weapon, the elite's
   crown and sparks (fixed tiles). */
#ifndef OBJ2_H
#define OBJ2_H
#include <stdint.h>
#ifndef OBJ_ON
#define OBJ_ON 0
#endif
#if OBJ_ON
#define NMOB 3
#define FX_NONE 0
#define FX_HIT 1  /* white flash */
#define FX_TELL 2 /* yellow / white blink: the wind-up tell */
#define FX_DEAD 3 /* fades to the haze */
typedef struct {
  uint16_t x, y;   /* 8.8 cells */
  uint8_t on, var; /* var: gen_bb.py PICKS index (2 = the elite) */
  uint8_t frame;   /* BB_* */
  uint8_t fx, fxt; /* FX_* and its timer (views) */
} mob_t;
extern mob_t mob[NMOB];
extern uint8_t weapon_frame; /* 0 idle, 1 strike, 0xFF hidden */
extern uint8_t obj_count;    /* OAM entries of the last built view (debug / measure) */
void obj_init(void);
void obj_build(uint8_t cur);
void obj_vbl_upload(uint8_t b);
void obj_vbl_flip(uint8_t b);
#else
#define obj_init()
#define obj_build(c)
#endif
#endif
