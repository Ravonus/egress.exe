#ifndef EGRESS_H
#define EGRESS_H
/* What the cartridge's modules share. Globals the PyBoy harness reads or pokes are plain (non-static) globals, so the
 * linker's .noi names them. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>
#include "core/eg_room.h"

/* game modes (game_mode) */
enum { MODE_BOOT = 0, MODE_TITLE = 1, MODE_PLAY = 2, MODE_CLEAR = 3, MODE_DEAD = 4, MODE_SETUP = 5 };

extern uint8_t game_mode;
extern uint16_t run_seed;
extern uint8_t floor_no; /* 1-based */
extern uint16_t floor_seed;
extern uint8_t floor_tier;
extern eg_room __at(0xD800) room;
extern uint16_t frame_count;

/* input, read once per frame */
extern uint8_t joy, joy_prev, joy_pressed;
void input_poll(void);

/* harness hooks: zero on power-on; the harness pokes them before pressing START */
extern uint8_t dbg_god;        /* the player takes no damage */
extern uint8_t dbg_no_enemies; /* floors spawn no enemies */
extern uint16_t dbg_seed;      /* nonzero: the run seed */
extern uint8_t dbg_floor;      /* nonzero: the run starts on this floor (10: the Watchman) */
extern uint16_t dbg_fseed;     /* B1 harness: nonzero: the next floor is this room seed, at tier dbg_ftier */
extern uint8_t dbg_ftier;

/* main.c: frame pacing and the LCD split (sprites off under the HUD) */
extern uint8_t hud_line; /* LY at which sprites turn off; 144 = never */
void frame_wait(void);

/* screens.c */
void keel_boot(void) BANKED; /* src/boot/keel_boot.c: CRUCIBLE's KEEL start-up */
extern uint8_t screen_dark; /* the LCD is on with every palette black: the next screen loads under it */
void title_screen(void) BANKED;
void dead_screen(void) BANKED;

/* play.c: plays floors until the run ends */
void play_run(void) BANKED;

/* sound.c */
enum {
  SFX_NONE = 0,
  SFX_STEP, SFX_DIG, SFX_TOKEN, SFX_KEY, SFX_DOOR, SFX_LASH, SFX_HIT, SFX_HURT,
  SFX_ROLL, SFX_CHARGED, SFX_SPIN, SFX_BREAK, SFX_THUD, SFX_PUSH, SFX_TELL, SFX_SHOT,
  SFX_KILL, SFX_DENY, SFX_EXIT,
  SFX_TEAR, SFX_CRUNCH, SFX_SHATTER, SFX_ZAP, /* fragile floors breaking (src/look.c look_break_tile) */
  /* B2 bosses (src/boss_*.c): each tell has its own sound */
  SFX_B_RISE, SFX_B_WHOOSH, SFX_B_BEAM, SFX_B_RUMBLE, SFX_B_ROAR, SFX_B_CLINK, SFX_B_RAM, SFX_B_JAM, SFX_B_DOWN,
  /* end B2 bosses */
  SFX_COUNT
};
enum { SONG_NONE = 0, SONG_LOBBY = 1, SONG_FLOOR = 2, SONG_BOSS = 3 };
void sound_init(void) BANKED;
void sound_tick(void) BANKED;
void sfx(uint8_t id) BANKED;
void music_play(uint8_t song) BANKED;
void music_fade(uint8_t out) BANKED; /* 1: fade the music out over ~1 s, 0: back to full */
extern uint8_t music_volume; /* 0..7, the master music level (options later) */
extern uint8_t sfx_volume;   /* 0..8 */
extern uint8_t music_song;

/* palettes: the live copies and a fade level (0 = black .. 8 = full) */
extern uint16_t pal_bg[32], pal_sp[32];
extern uint8_t pal_dirty;
void pal_commit(uint8_t level);
void fade_to(uint8_t from, uint8_t to);

/* text on a background or window map, font tiles in VRAM bank 1 */
void text_at(uint8_t win, uint8_t x, uint8_t y, const char *s, uint8_t pal);
void num_at(uint8_t win, uint8_t x, uint8_t y, uint16_t n, uint8_t digits, uint8_t pal);
void clear_map(uint8_t win, uint8_t pal);

#endif
