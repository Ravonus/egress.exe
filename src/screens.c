/* The title and the screen after a run ends (the KEEL start-up is src/boot/keel_boot.c). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "egress.h"
#include "gen/art.h"
#include "clock.h"
#include "boss_api.h"
#include "title3d/t3d.h"

static void screen_reset(void) {
  if (screen_dark)
    screen_dark = 0;
  else
    DISPLAY_OFF;
  HIDE_SPRITES;
  HIDE_WIN;
  hud_line = 144u;
  move_bkg(0, 0);
  {
    uint8_t i;
    for (i = 0; i != 40u; i++) move_sprite(i, 0, 0);
  }
}

static uint8_t wait_frames(uint8_t n, uint8_t skip_on_press) {
  while (n--) {
    frame_wait();
    sound_tick();
    input_poll();
    if (skip_on_press && (joy_pressed & (J_A | J_START))) return 1;
  }
  return 0;
}

/* The PRESS START title is the 3D walk in src/title3d (the logo, PRESS START, the clock row and the seed are drawn
 * there). It returns with every palette black and the LCD on, after START/A (play) or SELECT (SETUP). */
void title_screen(void) BANKED {
  while (1) {
    game_mode = MODE_TITLE;
    screen_reset();
    if (title3d_run() == T3D_SELECT) { /* SETUP: the clock (and later the other settings) */
      setup_screen();
      continue;
    }
    run_seed = dbg_seed ? dbg_seed : (uint16_t)(DIV_REG ^ (frame_count << 3) ^ (t3d_seed << 8) ^ t3d_vbl);
    if (!run_seed) run_seed = 0x2b1du;
    if (clock_needs_ask()) clock_screen(1); /* a new save asks once: WHAT IS TODAY? */
    return;
  }
}

static const char *const last_words[] = {
    "THE CARPET KEEPS IT.",
    "THE HUM GOT LOUDER.",
    "SAVED OVER, AGAIN.",
    "THE DOOR WAS DRAWN.",
};

void dead_screen(void) BANKED {
  char caption[20];
  game_mode = MODE_DEAD;
  screen_reset();
  art_load_game();
  clear_map(0, 4);
  text_at(0, 3, 4, "YOU STOPPED.", 4);
  if (boss_floor && boss_result == 2u) {
    boss_death_caption(caption);
    text_at(0, 0, 7, caption, 4);
  } else text_at(0, 0, 7, last_words[(uint8_t)(run_seed + floor_no) & 3u], 4);
  text_at(0, 3, 10, "FLOOR", 4);
  num_at(0, 12, 10, floor_no, 2, 4);
  text_at(0, 3, 13, "PRESS A", 4);
  pal_commit(0);
  SHOW_BKG;
  DISPLAY_ON;
  fade_to(0, 8);
  wait_frames(40u, 0);
  while (1) {
    frame_wait();
    sound_tick();
    input_poll();
    if (joy_pressed & (J_A | J_START)) break;
  }
  fade_to(8, 0);
}
