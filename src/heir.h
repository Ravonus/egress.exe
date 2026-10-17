#ifndef HEIR_H
#define HEIR_H
/* The heir on the floor: the genome's looks (the D3 sprite, composed at run time from baked parts, tools/bake_heir.py),
 * its abilities as the moveset reads them, and the animation player with the idle scheduler (proto/anim/heirs.py). */
#include <gb/gb.h>
#include <stdint.h>
#include "gen/heir_gen.h"
/* Canonical composed pose in WRAM, also used by native menu portraits.
 * Reading sprite VRAM while the LCD is active is not portable to hardware. */
extern uint8_t heir_pixels[128];
#include "core/eg_idle.h"

typedef struct {
  /* looks: shape genes (the composed sprite) and colour genes (the two OBJ palettes) */
  uint8_t skin;   /* 0..5, light to deep */
  uint8_t look;   /* 0 DREAM, 1 BOY, 2 GIRL */
  uint8_t hair;   /* as the portrait draws it: 0 bald, 1 short, 2 long, 3 bun, 4 spikes */
  uint8_t neck;   /* 0..3 */
  uint8_t pat;    /* 0..7 cloth pattern */
  uint8_t mark;   /* 0..7 worn mark */
  uint8_t accent; /* 0..7 cloth colour as worn (a full affinity dyes it) */
  /* abilities */
  uint8_t weapon; /* 0 blade 1 hammer 2 whip 3 gauntlet 4 launcher */
  uint8_t dodge;  /* EG_DODGE_* (roll, blink, slide) */
  uint8_t traits; /* HT_* expressed (at most two) */
  uint16_t seed;  /* the heir's own seed (idle fidgets) */
} heir_t;
#define HT_HEAVY 0x01u
#define HT_SMALL 0x02u
#define HT_GLITCHED 0x04u
#define HT_INSOMNIAC 0x08u
#define HT_OTHER 0x10u /* any of the other six (no effect on the floor yet) */

extern heir_t heir;
extern uint16_t dbg_heir;      /* harness: nonzero = the heir's seed instead of the run's */
extern uint8_t dbg_heir_state; /* harness: nonzero = play HA_(n-1) on loop, the player frozen (state sheets) */
extern uint8_t dbg_heir_dir;   /* with dbg_heir_state: the facing, 0..7 (S SE E NE N NW W SW) */
extern uint8_t opt_move4;      /* SETUP > MOVE: 0 8-way (default), 1 4-way (most recent press wins); this power-on
                                * only until a settings record exists in SRAM */
#ifndef EG_OPT_MOVE4
#define EG_OPT_MOVE4 0u        /* build-time: -DEG_OPT_MOVE4=1 makes 4-way the only mode */
#endif

void heir_roll(uint16_t seed) BANKED; /* genes.founder(Rng(seed * 0x9E3779B1 ^ 0x5BD1E995)) expressed */
void heir_colours(void) BANKED;      /* the BODY and WEAR palettes into pal_sp 0 and 1 */

/* the animation player */
extern uint8_t ha_state, ha_frame, ha_dir; /* HA_*, frame within it, facing 0..7 (S SE E NE N NW W SW) */
extern uint8_t ha_done;                    /* a one-shot state reached its end */
void heir_play(uint8_t state, uint8_t restart) BANKED;
void heir_idle(uint8_t ctx) BANKED; /* call every idle tick: the scheduler picks the state (EG_IC_* context) */
void heir_idle_stop(void) BANKED;   /* any input: the next idle starts a new plan */
void heir_tick(void) BANKED;        /* advance one tick (from draw_sprites: hitstop freezes it) */
void heir_prepare(void) BANKED;     /* reserve early frame time for a deferred complete pose */
void heir_reset(void) BANKED;       /* the floor's art was reloaded: compose again */
/* draw the heir with its box's top-left at (wx, wy) screen-world coordinates (sprites.c's convention); returns the
 * OAM entries used. pal_flash: 0 own palettes, else that OBJ palette for both layers */
uint8_t heir_draw(uint8_t oam, int16_t sx, int16_t sy) BANKED;

/* player.c (B1) */
extern uint8_t pl_iframe; /* dodge i-frames this frame */
extern uint8_t pl_arc;    /* the lead's arc: 0 none, 1 a strike toward pl_face, 2 the spin */
extern uint8_t pl_level;  /* the last charged attack's level */
void player_floor_start(void) BANKED; /* psx, psy set: the resolver, the mover, the spawn animation */

/* bank 0 (heir_draw.c) */
void heir_get_frame(uint8_t state, uint8_t frame, heir_frame_t *out) NONBANKED;
uint8_t heir_state_len(uint8_t state, uint8_t *loop) NONBANKED;
/* compose a frame record for this heir into tiles tile0..tile0+7; returns a mask of the non-empty sprites */
uint8_t heir_compose(uint8_t pose, uint8_t dd, uint8_t tile0) NONBANKED;
void heir_compose_init(void) NONBANKED; /* the heir's delta slots and the chunks' banks */
extern volatile uint8_t heir_pending; /* one WRAM pose awaits its VBlank upload */
void heir_get_pal(uint8_t skin, uint8_t accent, uint16_t *out8) NONBANKED; /* BODY 0..3, cloth into 5, 6 */

#endif
