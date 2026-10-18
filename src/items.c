#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "look.h"
#include "core/eg_piece.h"
#include "world.h"
#include "gen/world_art.h"
#include "gen/item_idle.inc"
/* One card per four frames: 64 bytes to existing BG-only tiles, no additional OAM or VRAM. */
void items_tick(void) BANKED {
  uint8_t k,p,t;
  if(world_on)world_art_tick(&meta_tile[0][0],frame_count);
  else environment_art_tick(&meta_tile[0][0],frame_count);
  if((room.portal_need&EG_PORTAL_ON) && !(frame_count&15u)) {
    if(room.portal[0]<EG_N)queue_cell(room.portal[0]);
    if(room.portal[1]<EG_N)queue_cell(room.portal[1]);
  }
  if ((frame_count&3u) || !room.nitem) return;
  k=(uint8_t)((frame_count>>2)%room.nitem);
  if (cell_type(room.item_cell[k]) != T_ITEM || !look_here(room.item_cell[k])) return;
  p=room.item_piece[k];
  if(!p || p>=P_COUNT) return;
  t=meta_tile[M_ITEM0+p-1u][0];
  VBK_REG=0;
  set_bkg_data(t,4u,&item_idle[(frame_count>>5)&3u][(uint16_t)(p-1u)<<6]);
}
