/* B2 rest landing (docs/redesign.md 4): between segments the landing's talker and trader (run.c landing_screen),
 * then the rest: GO ON, THE BOOK (the recipes the bench knows), THE TREE (B3's lineage: a stub that says so). */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "run.h"
#include "stage.h"
#include "core/eg_piece.h"

uint8_t rest_sel; /* the harness reads which line is chosen */
static char rb[12];

static void rest_wait_a(void) {
  while (1) {
    frame_wait();
    sound_tick();
    input_poll();
    if (joy_pressed & (J_A | J_B)) return;
  }
}

static void rest_screen(void) {
  DISPLAY_OFF;
  clear_map(0, 4);
  text_at(0, 2, 1, "THE LANDING RESTS", 4);
}
static void rest_show(void) {
  pal_commit(0);
  SHOW_BKG;
  DISPLAY_ON;
  fade_to(0, 8);
}

/* the book: every pair of found pieces that makes something, and what */
static void rest_book(void) {
  uint8_t a, b, r, y = 3u, x;
  rest_screen();
  text_at(0, 2, 1, "THE BOOK         ", 4);
  for (a = P_FIRE; a <= P_METAL; a++)
    for (b = a; b <= P_METAL; b++) {
      r = eg_recipe(a, b);
      if (!r || y > 15u) continue;
      eg_piece_abbr(a, rb);
      text_at(0, 1, y, rb, 4);
      text_at(0, 5, y, "+", 5);
      eg_piece_abbr(b, rb);
      text_at(0, 6, y, rb, 4);
      text_at(0, 10, y, "=", 5);
      eg_piece_name(r, rb);
      x = 11u;
      text_at(0, x, y, rb, 4);
      y++;
    }
  text_at(0, 2, 17, "B BACK", 5);
  rest_show();
  rest_wait_a();
  fade_to(8, 0);
}

static void rest_tree(void) {
  rest_screen();
  text_at(0, 2, 1, "THE TREE         ", 4);
  text_at(0, 2, 5, "THE LINEAGE IS", 4);
  text_at(0, 2, 6, "NOT WRITTEN YET.", 4);
  text_at(0, 2, 8, "RUNS SO FAR", 5);
  num_at(0, 14, 8, meta.runs, 3, 5);
  text_at(0, 2, 17, "B BACK", 5);
  rest_show();
  rest_wait_a();
  fade_to(8, 0);
}

void stage_landing(void) BANKED {
  uint8_t k;
  landing_screen();
  rest_sel = 0;
  while (1) {
    rest_screen();
    text_at(0, 3, 6, "GO ON", 4);
    text_at(0, 3, 8, "THE BOOK", 4);
    text_at(0, 3, 10, "THE TREE", 4);
    text_at(0, 2, 15, "A CHOOSE", 5);
    rest_show();
    while (1) {
      for (k = 0; k != 3u; k++) text_at(0, 1, (uint8_t)(6u + k * 2u), k == rest_sel ? ">" : " ", 5);
      frame_wait();
      sound_tick();
      input_poll();
      if ((joy_pressed & J_UP) && rest_sel) {
        rest_sel--;
        sfx(SFX_STEP);
      }
      if ((joy_pressed & J_DOWN) && rest_sel < 2u) {
        rest_sel++;
        sfx(SFX_STEP);
      }
      if (joy_pressed & J_A) break;
    }
    sfx(SFX_KEY);
    fade_to(8, 0);
    if (rest_sel == 0u) return;
    if (rest_sel == 1u) rest_book();
    else rest_tree();
  }
}
