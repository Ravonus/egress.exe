/* The title and the screen after a run ends (the KEEL start-up is src/boot/keel_boot.c). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "egress.h"
#include "gen/art.h"
#include "clock.h"

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

void title_screen(void) BANKED {
  uint8_t blink, setup;
  while (1) {
    blink = 0;
    setup = 0;
    game_mode = MODE_TITLE;
    screen_reset();
    art_load_game(); /* the font into VRAM bank 1 */
    art_show_picture(1, pal_bg);
    text_at(0, 4, 15, "PRESS START", 5);
    clock_title_line(0);
    text_at(0, 3, 17, "A KEEL CARTRIDGE", 5);
    pal_commit(0);
    SHOW_BKG;
    DISPLAY_ON;
    music_play(SONG_LOBBY);
    music_fade(0);
    fade_to(0, 8);
    while (1) {
      frame_wait();
      sound_tick();
      input_poll();
      blink++;
      if ((blink & 31u) == 0u) {
        text_at(0, 4, 15, (blink & 32u) ? "           " : "PRESS START", 5);
        if ((blink & 63u) == 0u) clock_title_line((uint8_t)(blink >> 6));
      }
      if (joy_pressed & J_SELECT) {
        setup = 1;
        break;
      }
      if (joy_pressed & (J_START | J_A)) break;
    }
    if (setup) { /* SETUP: the clock (and later the other settings) */
      sfx(SFX_STEP);
      fade_to(8, 0);
      setup_screen();
      continue;
    }
    sfx(SFX_EXIT);
    run_seed = dbg_seed ? dbg_seed : (uint16_t)(DIV_REG ^ (frame_count << 3) ^ ((uint16_t)blink << 8));
    if (!run_seed) run_seed = 0x2b1du;
    music_fade(1);
    fade_to(8, 0);
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
  game_mode = MODE_DEAD;
  screen_reset();
  art_load_game();
  clear_map(0, 4);
  text_at(0, 3, 4, "YOU STOPPED.", 4);
  text_at(0, 0, 7, last_words[(uint8_t)(run_seed + floor_no) & 3u], 4);
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
