#include <gb/gb.h>
#include <gb/cgb.h>
#include "catalogue_art.h"
uint16_t run_seed,pal_sp[32];
volatile uint16_t fusion_a=1u,fusion_b=2u,fusion_result=13u;
volatile uint8_t fusion_step,fusion_mode,fusion_pose,fusion_request=1u,fusion_done,fusion_buffer;
void main(void){
 uint8_t k,side,tile;int16_t x;
 DISPLAY_OFF;cpu_fast();SPRITES_8x16;HIDE_BKG;HIDE_WIN;SHOW_SPRITES;DISPLAY_ON;
 while(1){
  vsync();if(!fusion_request)continue;fusion_done=0u;fusion_buffer=(fusion_request&1u)*32u;tile=fusion_buffer;
  if(!fusion_mode)catalogue_bench_body(fusion_result,fusion_pose,tile);
  else {if(fusion_mode==1u)catalogue_fusion_begin(fusion_a,fusion_b,fusion_result);catalogue_fusion_step(fusion_step,tile);}
  catalogue_art_palette(fusion_mode && fusion_step<9u ? fusion_a : fusion_result,6u,1u);
  catalogue_art_palette(fusion_step<9u ? fusion_b : fusion_result,7u,1u);
  for(side=0u;side<2u;side++)for(k=0u;k<8u;k++){
   x=fusion_mode ? (side ? 80-fusion_step : 48+fusion_step) : 64;
   set_sprite_tile(side*8u+k,tile+side*16u+k*2u);set_sprite_prop(side*8u+k,8u|6u|side);
   move_sprite(side*8u+k,(uint8_t)(x+(k&3u)*8u+8u),(uint8_t)(52u+(k>>2)*16u));
   if(!fusion_mode && side)move_sprite(side*8u+k,0u,0u);
  }
  fusion_done=fusion_request;fusion_request=0u;
 }
}
