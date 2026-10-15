/* Bank 0: the floor's HUD in the window, in the HUD palette (LOOK_PAL_HUD: ink, pink, accent, white).
 *   row top+0  the rim: the charge bar on the left, a white-over-pink rim line across the rest
 *   row top+1  hearts, tokens, keycards, the floor
 * A message is a toast above the HUD: the window rises 8 px (row 0 is the message, centred) for as long as it shows,
 * so the rim always separates it from the counters. Bank 0 so hud_msg can take a string from whichever bank calls. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"

static uint8_t msg_t, top;
#define HUD_Y 128u   /* the window's top without a toast */
#define TOAST_Y 120u /* with one */

static void hud_tile(uint8_t x, uint8_t y, uint8_t t, uint8_t pal) {
  uint8_t a = (uint8_t)(pal | 0x08u);
  VBK_REG = 1;
  set_win_tiles(x, y, 1, 1, &a);
  VBK_REG = 0;
  set_win_tiles(x, y, 1, 1, &t);
}

static const char blank20[] = "                    ";
static void hud_layout(uint8_t toast) {
  uint8_t i;
  top = toast;
  for (i = 0; i != 3u; i++) text_at(1, 0, i, blank20, LOOK_PAL_HUD);
  move_win(7, toast ? TOAST_Y : HUD_Y);
  hud_line = toast ? TOAST_Y : HUD_Y;
  for (i = 5u; i != 20u; i++) hud_tile(i, top, BT_RIM, LOOK_PAL_HUD);
  hud_charge();
  hud_draw();
}

void hud_init(void) NONBANKED {
  msg_t = 0;
  hud_layout(0);
}

void hud_msg(const char *s) NONBANKED {
  uint8_t n = 0;
  while (s[n]) n++;
  if (!n) {
    msg_t = 1u; /* closes on the next tick */
    return;
  }
  if (!top) hud_layout(1u);
  else text_at(1, 0, 0, blank20, LOOK_PAL_HUD);
  text_at(1, (uint8_t)((20u - (n > 20u ? 20u : n)) >> 1), 0, s, LOOK_PAL_HUD);
  msg_t = 150u;
}

void hud_tick(void) NONBANKED {
  if (msg_t && !--msg_t) hud_layout(0);
  if (hud_dirty) hud_draw();
}

void hud_draw(void) NONBANKED {
  uint8_t i, h, y = (uint8_t)(top + 1u);
  for (i = 0; i != (uint8_t)(pl_hp_max >> 1); i++) {
    h = pl_hp > (uint8_t)(i * 2u + 1u) ? 2u : (pl_hp > (uint8_t)(i * 2u) ? 1u : 0u);
    hud_tile(i, y, (uint8_t)(h == 2u ? BT_HEART2 : (h == 1u ? BT_HEART1 : BT_HEART0)), LOOK_PAL_HUD);
  }
  hud_tile(5, y, BT_ICON_TOKEN, LOOK_PAL_HUD);
  num_at(1, 6, y, tokens, 3, LOOK_PAL_HUD);
  if (boss_floor) {
    text_at(1, 10, y, "   ", LOOK_PAL_HUD);
  } else {
    hud_tile(10, y, BT_ICON_KEY, LOOK_PAL_HUD);
    num_at(1, 11, y, keys_held, 1, LOOK_PAL_HUD);
    text_at(1, 12, y, "/", LOOK_PAL_HUD);
    num_at(1, 13, y, room.keys, 1, LOOK_PAL_HUD);
  }
  text_at(1, 16, y, "F", LOOK_PAL_HUD);
  num_at(1, 17, y, floor_no, 2, LOOK_PAL_HUD);
  hud_dirty = 0;
}

void hud_charge(void) NONBANKED {
  uint8_t i, full = CHARGE_FULL;
  uint8_t lit = pl_charge >= full ? 40u : (uint8_t)((uint16_t)pl_charge * 40u / full);
  for (i = 0; i != 5u; i++) {
    uint8_t n = lit > 8u ? 8u : lit;
    hud_tile(i, top, (uint8_t)(BT_BAR + n), LOOK_PAL_HUD);
    lit = (uint8_t)(lit - n);
  }
}
