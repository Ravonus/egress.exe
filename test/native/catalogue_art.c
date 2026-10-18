/* Isolated cartridge gate for the exact banked assets used by the game. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "catalogue_art.h"
uint16_t run_seed;
uint16_t pal_sp[32];
volatile uint16_t art_test_id=433u;
volatile uint8_t art_test_direction=4u,art_test_state,art_test_pose,art_test_request=1u,art_test_done,art_test_palette;
void main(void) {
 uint8_t tile,flip;
 DISPLAY_OFF;SPRITES_8x16;HIDE_BKG;HIDE_WIN;SHOW_SPRITES;
 set_sprite_tile(0u,0u);set_sprite_tile(1u,2u);
 move_sprite(0u,72u,72u);move_sprite(1u,80u,72u);
 DISPLAY_ON;
 while(1) {
  vsync();
  if(!art_test_request)continue;
  art_test_done=0u;tile=(art_test_request&1u) ? 4u : 0u;
  catalogue_art_upload(art_test_id,(art_test_state&7u)|((art_test_direction&7u)<<3),art_test_pose,tile);
  catalogue_art_palette(art_test_id,art_test_palette&7u,1u);
  flip=art_test_direction>4u ? S_FLIPX : 0u;
  set_sprite_tile(0u,tile+(flip ? 2u : 0u));set_sprite_tile(1u,tile+(flip ? 0u : 2u));
  set_sprite_prop(0u,flip|(art_test_palette&7u));set_sprite_prop(1u,flip|(art_test_palette&7u));
  art_test_done=art_test_request;art_test_request=0u;
 }
}
