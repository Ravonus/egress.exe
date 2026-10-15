#ifndef BOSS_INT_H
#define BOSS_INT_H
/* Inside the B2 bosses (src/boss_*.c). Plain globals: the PyBoy harness reads them by name (test/harness/boss.py). */
#include "boss_api.h"
#include "gen/boss_art.h"

/* attacks (one id per attack family: the tell, the sound and the answer never change between heirs) */
enum {
  ATK_NONE = 0,
  ATK_SLAM,   /* a stamp drops on a shadow that followed you, then held still: walk out or roll (dodge) */
  ATK_SWEEP,  /* a stamp crosses a row from a wall: change rows or roll through it; a perfect parry stops it */
  ATK_VOLLEY, /* the screen fires three orbs: parry them back into it */
  ATK_BEAM,   /* the screen burns a marked column: leave it (then its screen is open) */
  ATK_QUAKE,  /* a shelf cracks and its CRT drops down the column: leave it */
  ATK_FLOOR,  /* a cracked row of floor falls: be off it */
  ATK_RAM,    /* LAST FAX rams down a marked lane: step aside or roll */
  ATK_JAM,    /* LAST FAX's paper-jam burst round itself: get clear or roll */
  ATK_FAN,    /* LAST FAX spits a fan of pages: parry them back */
  ATK_CONTACT,
  ATK_N
};
/* the boss's state (bs_st) */
enum { BS_INTRO = 0, BS_IDLE, BS_ATTACK, BS_GATE, BS_DOWN, BS_SHARD, BS_EXIT, BS_DEAD };
/* a stamp's (or the fax's) state */
enum { HS_HOME = 0, HS_RISE, HS_DROP, HS_REST, HS_BACK, HS_PREP, HS_SWEEP, HS_FALL };

extern uint8_t bs_kind, bs_phase, bs_hp, bs_st, bs_t, bs_atk, bs_seq, bs_hits, bs_open, bs_face, bs_flash;
extern uint8_t bs_tx, bs_ty;          /* the attack's target (world px): a shadow, a column, a row, a lane */
extern uint8_t bs_x, bs_y;            /* LAST FAX's centre, or the Tenant's screen */
extern uint8_t bs_hx[2], bs_hy[2], bs_hs[2], bs_ht[2], bs_htx[2], bs_hty[2]; /* the stamps: pos, state, timer, aim */
extern uint8_t bs_harm;               /* the stamps wear armour (phase 3) */
extern int8_t bs_vx, bs_vy;           /* LAST FAX's ram */
extern uint16_t bs_clock;             /* frames in this phase (the enrage) */
extern uint8_t bs_dmg[ATK_N], bs_mist[ATK_N]; /* half-hearts taken and hits taken, per attack (the profile) */
extern uint8_t bs_cause_atk;          /* the attack whose hit is landing now */
extern uint8_t bs_shard_x, bs_shard_y;
extern uint8_t bs_drop_row;           /* the enrage's next row */
extern uint8_t bs_pal_dirty;          /* boss_vblank: 1 body/screen palettes, 2 OBJ 3, 4 the face tiles, 8 dim */
extern uint8_t bs_dim;                /* the lights: 0 on .. 4 out */

/* boss_core.c helpers for the two bosses */
void bc_hurt(uint8_t n, uint8_t atk, uint8_t fromx, uint8_t fromy) BANKED; /* through player_hurt, attributed */
/* the same; returns 0 no effect, 1 hurt, 2 the guard took it, 3 a perfect parry took it */
uint8_t bc_hurt_r(uint8_t n, uint8_t atk, uint8_t fromx, uint8_t fromy) BANKED;
void orbs_clear(void) BANKED;          /* every orb gone (a phase gate) */
void boss_set_dim(uint8_t d) BANKED;   /* the lights: 0 on .. 4 half */
uint8_t tenant_beam(void) BANKED;      /* 0 none, 1 the column's tell, 2 burning */
void bc_tell(uint8_t sfx_id) BANKED;   /* a tell starts: its sound, the palette flash */
void bc_face(uint8_t v) BANKED;        /* the Tenant's screen: BA_FACE_* (written in VBlank) */
void bc_gate(void) BANKED;             /* the phase's HP is gone: the next phase's beat, or the boss falls */
uint8_t bc_floor_at(uint8_t x, uint8_t y) BANKED; /* open floor a body can stand on (px) */
void bc_break_shelf(uint8_t c) BANKED; /* a shelf's drywall gives: its CRT falls down the column */
/* the bosses */
void tenant_setup(void) BANKED;
void tenant_update(void) BANKED;
void tenant_gate(uint8_t to) BANKED;   /* the beat into phase `to` (1 the lights, 2 the floor) */
void fax_setup(void) BANKED;
void fax_update(void) BANKED;
void fax_gate(uint8_t to) BANKED;
/* boss_art.c (its own bank: the baked data) */
void boss_art_load(uint8_t kind) BANKED;              /* display off: tiles */
void boss_art_body(void) BANKED;                       /* display off: the Tenant into the BG map */
void boss_art_face(uint8_t v) BANKED;                  /* in VBlank: the screen's 8 tiles */
void boss_art_pals(uint8_t kind, uint16_t *bg1, uint16_t *bg7, uint16_t *obj3) BANKED;

/* the Tenant's place: its body's top-left cell, its screen's centre (world px) */
#define TEN_CX 6u
#define TEN_CY 3u
#define TEN_FACE_X 128u
#define TEN_FACE_Y 72u
/* the arena on screen: cells 3..12 x 3..10; open floor from row 5 (the sides) and row 6 (under the body) */
#define AR_X0 3u
#define AR_X1 12u
#define AR_Y1 10u
#define SFX_B_SLAM SFX_THUD

#endif
