/* Bank 0: the HUD in the window's two rows (hearts, tokens, keycards, floor; the charge bar and a message line).
 * Bank 0 so hud_msg can take a string literal from whichever bank calls it. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"

static uint8_t msg_t;

static void hud_tile(uint8_t x, uint8_t y, uint8_t t, uint8_t pal) {
  uint8_t a = (uint8_t)(pal | 0x08u);
  VBK_REG = 1;
  set_win_tiles(x, y, 1, 1, &a);
  VBK_REG = 0;
  set_win_tiles(x, y, 1, 1, &t);
}

void hud_msg(const char *s) NONBANKED {
  text_at(1, 6, 1, "              ", 4);
  text_at(1, 6, 1, s, 4);
  msg_t = 150u;
}

void hud_tick(void) NONBANKED {
  if (msg_t && !--msg_t) text_at(1, 6, 1, "              ", 4);
  if (hud_dirty) hud_draw();
}

void hud_draw(void) NONBANKED {
  uint8_t i, h;
  for (i = 0; i != (uint8_t)(pl_hp_max >> 1); i++) {
    h = pl_hp > (uint8_t)(i * 2u + 1u) ? 2u : (pl_hp > (uint8_t)(i * 2u) ? 1u : 0u);
    hud_tile(i, 0, (uint8_t)(h == 2u ? BT_HEART2 : (h == 1u ? BT_HEART1 : BT_HEART0)), 6);
  }
  hud_tile(5, 0, BT_ICON_TOKEN, 5);
  num_at(1, 6, 0, tokens, 3, 4);
  if (boss_floor) {
    text_at(1, 10, 0, "   ", 4);
  } else {
    hud_tile(10, 0, BT_ICON_KEY, 6);
    num_at(1, 11, 0, keys_held, 1, 4);
    text_at(1, 12, 0, "/", 4);
    num_at(1, 13, 0, room.keys, 1, 4);
  }
  text_at(1, 16, 0, "F", 4);
  num_at(1, 17, 0, floor_no, 2, 4);
  hud_dirty = 0;
}

void hud_charge(void) NONBANKED {
  uint8_t i, full = CHARGE_FULL;
  uint8_t lit = pl_charge >= full ? 40u : (uint8_t)((uint16_t)pl_charge * 40u / full);
  for (i = 0; i != 5u; i++) {
    uint8_t n = lit > 8u ? 8u : lit;
    hud_tile(i, 1, (uint8_t)(BT_BAR + n), 4);
    lit = (uint8_t)(lit - n);
  }
}
