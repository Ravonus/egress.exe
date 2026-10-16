#ifndef EG_MOVE_H
#define EG_MOVE_H
/* EGRESS 8-direction movement (proof module): free 8-way (or 4-way, most recent press wins) movement on 12.4
 * fixed-point positions, axis-separated collision against a grid of 16x16 cells with wall sliding and corner
 * nudging, 8-way facing, and a deliberate cardinal push request (the module never moves blocks itself).
 *
 * The d-pad byte uses the GBDK joypad() layout: J_RIGHT 0x01, J_LEFT 0x02, J_UP 0x04, J_DOWN 0x08 (other bits
 * ignored). All state is in eg_mover_t; the step is a pure function of state, input, speed and the grid.
 * Portable C for SDCC (SM83) and host compilers: no floats, no allocation, no globals. */
#include <stdint.h>
#ifndef EG_BANKED /* banked on the cartridge (B2: ROM0 is for interrupts and hot paths); also in eg_room.h */
#ifdef __SDCC
#include <gb/gb.h>
#define EG_BANKED BANKED
#else
#define EG_BANKED
#endif
#endif

#define EG_DP_RIGHT 0x01u
#define EG_DP_LEFT 0x02u
#define EG_DP_UP 0x04u
#define EG_DP_DOWN 0x08u
#define EG_DP_MASK 0x0fu

#define EG_CELL_PX 16
#define EG_BOX_PX 12        /* player hitbox: 12x12 px, position = its top-left corner */
#define EG_FIX 4            /* 12.4 fixed point: 16 sub-pixels per pixel */
#define EG_DIAG_NUM 181u    /* diagonal component = speed * 181/256 (0.7070), remainder carried per frame */
#define EG_NUDGE_PX 4       /* corner nudge when the box overlaps the blocking cell by <= 4 px */
#define EG_PUSH_HOLD 10u    /* frames of continuous cardinal contact before a push request */

enum { EG_MODE_8WAY = 0, EG_MODE_4WAY };
enum { EG_FACE_N = 0, EG_FACE_NE, EG_FACE_E, EG_FACE_SE, EG_FACE_S, EG_FACE_SW, EG_FACE_W, EG_FACE_NW };
enum { EG_C_FREE = 0, EG_C_WALL = 1, EG_C_BLOCK = 2 }; /* grid cells: nonzero is solid, BLOCK is pushable */

#define EG_BLK_X 0x01u
#define EG_BLK_Y 0x02u

typedef struct {
  const uint8_t *cells; /* (on the cartridge the grid, its cells and map are in WRAM: eg_move is banked) row-major, w*h; outside the grid counts as wall */
  uint8_t w, h;
  uint8_t wshift; /* nonzero: w == 1 << wshift, rows are indexed by a shift (no multiply on the SM83) */
  const uint8_t *map; /* nonzero: cells are the game's own bytes and map[byte] is the EG_C_* cell (no copy per step) */
} eg_grid_t;

typedef struct {
  int16_t x, y;       /* hitbox top-left, 12.4 sub-pixels */
  uint8_t mode;       /* EG_MODE_* */
  uint8_t facing;     /* EG_FACE_*, kept while stopped */
  uint8_t prev;       /* last frame's d-pad */
  uint8_t order[4];   /* direction bits, most recently pressed first */
  uint8_t dir;        /* resolved direction bits this frame */
  uint8_t acc;        /* diagonal remainder (1/256 sub-pixel) */
  uint8_t blocked;    /* EG_BLK_* this frame */
  uint8_t push_t;     /* consecutive frames of contact with push_cx/cy/dir */
  uint8_t push_cx, push_cy, push_dir;
  uint8_t req;        /* 1 on the frame a push request is raised */
  uint8_t req_cx, req_cy, req_dir; /* the cell to push and the direction bit */
} eg_mover_t;

void eg_move_init(eg_mover_t *m, int16_t x, int16_t y, uint8_t mode) EG_BANKED;
void eg_move_step(eg_mover_t *m, uint8_t dpad, uint8_t speed, const eg_grid_t *g) EG_BANKED;
uint8_t eg_grid_cell(const eg_grid_t *g, int16_t cx, int16_t cy) EG_BANKED; /* EG_C_WALL outside the grid */
/* Integer aim: max(|vx|,|vy|)=speed, minor component truncated toward zero. */
void eg_aim(int16_t dx, int16_t dy, uint8_t speed, int8_t *vx, int8_t *vy) EG_BANKED;

#endif
