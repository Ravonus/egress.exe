#ifndef EG_INPUT_H
#define EG_INPUT_H
/* EGRESS two-button move resolver (proof module). See proto/input/FRAMES.md for the frame-data table and rules.
 *
 * Call eg_input_step() once per frame with the held-buttons byte (GBDK joypad() layout: J_A = 0x10, J_B = 0x20;
 * other bits are ignored). Press/release edges are computed inside. The whole state lives in eg_input_t: the
 * resolver is a pure function of the input stream and the dodge style. Frame counters start at 0 on the frame a
 * move starts.
 *
 * Portable C for SDCC (SM83) and host compilers: 8-bit arithmetic, no floats, no allocation, no globals but the
 * const frame tables. */
#include <stdint.h>
#ifndef EG_BANKED /* banked on the cartridge (B2: ROM0 is for interrupts and hot paths); also in eg_room.h */
#ifdef __SDCC
#include <gb/gb.h>
#define EG_BANKED BANKED
#else
#define EG_BANKED
#endif
#endif

#define EG_IN_A 0x10u
#define EG_IN_B 0x20u

#define EG_HOLD_T 11u      /* a press held for >= 11 frames (press frame counts as 1) is a hold; released earlier = tap */
#define EG_BUF_FRAMES 6u   /* a buffered press can fire on its press frame and the 5 frames after */
#define EG_AB_WIN 4u       /* B within 0..4 frames after A's press (or while A is held) = secondary/parry */
#define EG_CHARGE_L2 30u   /* A held 11..29 frames -> level 1, 30..49 -> level 2, >= 50 -> level 3 */
#define EG_CHARGE_L3 50u
#define EG_AGE_CAP 32u
#define EG_NO_MOVE 0xffu   /* eg_input_step(): no move started this frame */
#define EG_NO_LINK 0xffu

enum {
  EG_MV_IDLE = 0,
  EG_MV_DODGE,   /* B tap: roll / blink / slide (style) */
  EG_MV_SPRINT,  /* B hold: the dodge flows into a sprint while B stays held (loop) */
  EG_MV_SKID,    /* sprint brake after B is released */
  EG_MV_LUNGE,   /* B then A: dodge-cancel strike, keeps dodge momentum */
  EG_MV_HIT1,    /* A tap: combo hit 1 */
  EG_MV_HIT2,
  EG_MV_HIT3,
  EG_MV_CHARGE,  /* A hold: charging (loop) */
  EG_MV_CHARGED, /* A released after charging: the charged attack (level in .level) */
  EG_MV_PARRY,   /* A then B: weapon secondary / parry */
  EG_MV_COUNT
};

enum { EG_PH_NONE = 0, EG_PH_STARTUP, EG_PH_ACTIVE, EG_PH_RECOVERY, EG_PH_LOOP };
enum { EG_DODGE_ROLL = 0, EG_DODGE_BLINK, EG_DODGE_SLIDE, EG_DODGE_COUNT };
enum { EG_INT_NONE = 0, EG_INT_A, EG_INT_B, EG_INT_AB }; /* buffered intent */

#define EG_FL_IFRAME 0x01u   /* dodge active frames */
#define EG_FL_MOMENTUM 0x02u /* dodge, sprint, lunge: the mover keeps dodge velocity */
#define EG_FL_HITBOX 0x04u   /* attack active frames */
#define EG_FL_GUARD 0x08u    /* parry active frames */
#define EG_FL_PERFECT 0x10u  /* parry perfect window */

/* Frame data. Frames 0..startup-1 startup, then active, then recovery; total = sum. open/close: for hits and the
 * lunge, the combo-continue window is open..total-1 (close = total-1); for dodges, the B->A cancel window
 * open..close inclusive; for the parry, the perfect window open..close. EG_NO_LINK = none. Loop moves are all 0. */
typedef struct {
  uint8_t startup, active, recovery, open, close;
} eg_frames_t;

extern const eg_frames_t eg_input_fd[EG_MV_COUNT]; /* the EG_MV_DODGE row is unused: see eg_dodge_fd */
extern const eg_frames_t eg_dodge_fd[EG_DODGE_COUNT];
/* the tables (and eg_input_frames' pointer) are in eg_input's bank on the cartridge: other banks copy a row out */
void eg_input_fd_get(uint8_t move, eg_frames_t *out) EG_BANKED;

typedef struct {
  uint8_t style;  /* EG_DODGE_* */
  uint8_t prev;   /* last frame's held byte */
  uint8_t move;   /* EG_MV_* */
  uint8_t phase;  /* EG_PH_* */
  uint8_t frame;  /* frames since the move started (0 on its first frame), saturates at 255 for loops */
  uint8_t level;  /* charge level of the last EG_MV_CHARGED (1..3) */
  uint8_t flags;  /* EG_FL_* for the current frame */
  uint8_t chain;  /* 1: this dodge came out of a lunge, so it cannot cancel into another lunge */
  uint8_t a_hold, b_hold; /* frames the button has been held (0 when up), saturating */
  uint8_t a_age;  /* frames since A's last press edge, capped at EG_AGE_CAP */
  uint8_t buf, buf_age; /* one-slot input buffer: EG_INT_*, newest press wins */
} eg_input_t;

void eg_input_init(eg_input_t *s, uint8_t style) EG_BANKED;
/* Advance one frame. Returns the EG_MV_* started this frame (at most one), or EG_NO_MOVE. */
uint8_t eg_input_step(eg_input_t *s, uint8_t held) EG_BANKED;
const eg_frames_t *eg_input_frames(const eg_input_t *s, uint8_t move) EG_BANKED;
uint8_t eg_input_total(const eg_input_t *s, uint8_t move) EG_BANKED; /* 0 for idle and loop moves */
uint8_t eg_input_parry(const eg_input_t *s) EG_BANKED;               /* 0 none, 1 guard, 2 perfect */

#endif
