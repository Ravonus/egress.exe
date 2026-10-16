#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "look.h"
#include "core/eg_piece.h"
#include "gen/item_idle.inc"
/* One card per four frames: 64 bytes to existing BG-only tiles, no additional OAM or VRAM. */
void items_tick(void) BANKED {
  uint8_t k,p,t;
  if ((frame_count&3u) || !room.nitem) return;
  k=(uint8_t)((frame_count>>2)%room.nitem);
  if (cell_type(room.item_cell[k]) != T_ITEM || !look_here(room.item_cell[k])) return;
  p=room.item_piece[k];
  if(!p || p>=P_COUNT) return;
  t=(uint8_t)(LOOK_LOW0_BASE+(p-1u)*4u);
  VBK_REG=0;
  set_bkg_data(t,4u,&item_idle[(frame_count>>5)&3u][(uint16_t)(p-1u)<<6]);
}
