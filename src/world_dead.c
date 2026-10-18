/* ROM0 callbacks keep the scanner's pointer table valid across banked rules.
 * Constructor flood scratch becomes the fixed-WRAM active continuation. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "look.h"
#include "puzzle.h"
#include "world.h"
#include "world_dead.h"
#include "core/eg_land_dead.h"
#include "core/eg_stage.h"
#include "save_map.h"
typedef char world_dead_scratch_layout[(sizeof(eg_stage_work.world_dead)==sizeof(eg_land_dead)) ? 1 : -1];
static uint8_t known(void *ctx,uint8_t x,uint8_t y) NONBANKED {
  (void)ctx;
  if(x>=world_view.width || y>=world_view.height)return 0u;
  if(x>=world_view.x && x<world_view.x+16u && y>=world_view.y && y<world_view.y+14u)return 1u;
  return world_cached(x,y);
}
static uint8_t read(void *ctx,uint8_t x,uint8_t y) NONBANKED {return world_read(ctx,x,y);}
static void quad(void *ctx,uint8_t x,uint8_t y,uint8_t *out) NONBANKED {
  view_load(ctx,x,y,eg_window_packet);memcpy(out,eg_window_packet,4u);
}
static uint8_t mask(void *ctx,uint8_t pair,uint8_t byte) NONBANKED {
  (void)ctx;
  CRITICAL {view_get9((uint16_t)(0x4000u+(uint8_t)(pair*9u)),world_mask_packet);}
  return world_mask_packet[8]&look_bits[byte] ? world_mask_packet[byte] : 0u;
}
static const eg_land_dead_io io={known,read,quad,mask,0};
void world_dead_begin(void) NONBANKED {
  if(!world_on)return;
  eg_land_dead_begin((eg_land_dead *)eg_stage_work.world_dead);
}
uint8_t world_dead_step(uint8_t from,uint8_t budget) NONBANKED {
  if(!world_on || enemies_alive)return 0u;
  return eg_land_dead_step((eg_land_dead *)eg_stage_work.world_dead,&world_plan,&world_view,&room,from,
                          (const uint8_t *)SAVE_FUSION_SCRATCH,effect_n,world_latched,&io,budget);
}
