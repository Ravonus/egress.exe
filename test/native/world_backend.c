#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "look.h"
#include "world.h"
#include "world_dead.h"
#include "puzzle.h"
#include "core/eg_piece.h"
#include "core/eg_stage.h"
#include "world_cases.h"
#include "gen/world_art.h"
uint8_t eg_window_packet[9],world_mask_packet[9],world_pre_x[9],world_pre_y[9];
eg_stage_scratch eg_stage_work;
uint8_t meta_tile[M_COUNT][4],meta_pal[M_COUNT];
uint16_t pal_bg[32];
uint8_t eg_plates_dirty,eg_dirty[EG_DIRTY_MAX],eg_ndirty;
const uint8_t look_bits[8]={1,2,4,8,16,32,64,128};
uint8_t __at(0xC500) result[4];
uint8_t __at(0xC600) output[WG_BYTES];
uint8_t __at(0xDDD0) canary[32];
eg_room __at(0xD800) room;
uint8_t __at(0xDCF0) look_skin[224];
uint8_t cracked[28];
uint8_t __at(0xDBC0) pocket[POCKET_CAP];
uint8_t pocket_n,enemies_alive;
static const eg_land *wg_p(void) {return &world_plan;}
static eg_view *wg_v(void) {return &world_view;}
static eg_room *wg_r(void) {return &room;}
static uint8_t *wg_skin(void) {return look_skin;}
static uint8_t *wg_crack(void) {return cracked;}
static uint8_t wg_latched(void) {return world_latched;}
static uint8_t wg_begin(uint8_t which) {return world_begin((uint16_t)(which*997u+17u),(uint8_t)(which%6u),lc_sizes[which],(uint8_t)((which*3u)&7u));}
static uint8_t wg_read(uint8_t x,uint8_t y) {return world_read(0,x,y);}
static void wg_write(uint8_t x,uint8_t y,uint8_t raw) {(void)world_write(x,y,raw);}
static void wg_bind(void) {world_bind();}
static void wg_source(uint8_t occupied) {world_sources(&occupied,(uint8_t)(occupied<224u));}
static void wg_load(uint8_t x,uint8_t y) {world_cache_load(0,x,y,eg_window_packet);}
static uint8_t wg_shift(int8_t dx,int8_t dy) {
  uint8_t x,y,from,limit;
  if((dx<0 && !world_view.x) || (dy<0 && !world_view.y) ||
     (dx>0 && world_view.x==world_view.width-16u) || (dy>0 && world_view.y==world_view.height-14u))return world_shift_data(dx,dy);
  /* Keep failed/unprefetched moves on the defensive API; exercise the staged
   * path only once every incoming chunk is known, as the game controller does. */
  if(dx) {
    x=dx>0 ? world_view.x+16u : world_view.x-4u;
    for(y=0u;y<14u;y++)if(!world_cached(x,world_view.y+y))return world_shift_data(dx,dy);
    from=dx>0 ? 0u : 12u;
    for(y=0u;y<14u;y++)if(!world_chunk_preserved(from,y))world_preserve(from,y);
  } else {
    y=dy>0 ? world_view.y+14u : world_view.y-4u;
    for(x=0u;x<16u;x+=4u)for(from=0u;from<4u;from++)
      if(!world_cached(world_view.x+x,y+from))return world_shift_data(dx,dy);
    from=dy>0 ? 0u : 10u;limit=from+4u;
    for(y=from;y<limit;y++)for(x=0u;x<16u;x+=4u)if(!world_chunk_preserved(x,y))world_preserve(x,y);
  }
  if(!world_preserved(dx,dy))result[3]=10u;
  world_prepare_binding(dx,dy);
  if(!world_binding_ready(dx,dy))result[3]=12u;
  /* A material/skin/crack update must revoke the prepared certificate. */
  x=dx ? (dx>0 ? 0u : 12u) : 0u;y=dx ? 0u : dy>0 ? 0u : 10u;
  world_touch(EG_IDX(x,y));
  if(world_preserved(dx,dy) || world_binding_ready(dx,dy))result[3]=11u;
  world_preserve(x,y);
  world_prepare_binding(dx,dy);
  return world_shift_prepared(dx,dy);
}
static uint8_t warm_shift(int8_t dx,int8_t dy) {
  uint8_t x,y,k;
  if(dx) {
    x=dx>0 ? world_view.x+16u : world_view.x-4u;
    for(y=0u;y<14u;y++)world_cache_load(0,x,world_view.y+y,eg_window_packet);
  } else {
    y=dy>0 ? world_view.y+14u : world_view.y-4u;
    for(k=0u;k<4u;k++)for(x=0u;x<16u;x+=4u)world_cache_load(0,world_view.x+x,y+k,eg_window_packet);
  }
  return wg_shift(dx,dy);
}
static void retained_certificates(void) {
  int8_t dx,dy;uint8_t x,y,gx,gy,c,mask;
  if(!world_begin(17u,5u,2u,0u)) {result[3]=13u;return;}
  dx=world_view.x+8u<=world_view.width-16u ? 4 : -4;
  dy=world_view.y<world_view.height-14u ? 4 : -4;
  x=dx>0 ? 4u : 11u;gx=world_view.x+x;gy=world_view.y+6u;c=EG_IDX(x,6u);mask=look_bits[c&7u];
  (void)world_write(gx,gy,T_DIRT);look_skin[c]=M_WORLD_FERN0+1u;room.prot[c>>3]|=mask;cracked[c>>3]|=mask;world_touch(c);
  for(y=0u;y<14u;y++)for(x=0u;x<16u;x+=4u)world_preserve(x,y);
  if(!warm_shift(dx,0))result[3]=13u;
  c=eg_view_local(&world_view,gx,gy);if(c>=EG_N) {result[3]=13u;return;}mask=look_bits[c&7u];
  (void)world_write(gx,gy,T_WATER);look_skin[c]=M_WORLD_RIVER0+1u;room.prot[c>>3]&=(uint8_t)~mask;cracked[c>>3]&=(uint8_t)~mask;world_touch(c);
  /* This changed overlap survives a perpendicular transfer before leaving
   * the window. Its former certificate must not suppress the new packet. */
  if(!warm_shift(0,dy) || !warm_shift(dx,0) || !warm_shift(-dx,0) || !warm_shift(0,-dy) || !warm_shift(-dx,0))result[3]=13u;
  c=eg_view_local(&world_view,gx,gy);if(c>=EG_N) {result[3]=13u;return;}mask=look_bits[c&7u];
  if(room.c[c]!=T_WATER || look_skin[c]!=M_WORLD_RIVER0+1u || (room.prot[c>>3]&mask) || (cracked[c>>3]&mask))result[3]=13u;
}
void dead_after(void) NONBANKED NAKED {__asm ret __endasm;}
static uint16_t bitmap_hash(void) {
  uint16_t hash=0xBEEFu;uint8_t group,k;
  for(group=0u;group<27u;group++) {
    CRITICAL {view_get9((uint16_t)(0x4000u+(uint16_t)group*9u),world_mask_packet);}
    for(k=0u;k<9u;k++)hash=(uint16_t)((hash<<1)^(hash>>15)^world_mask_packet[k]);
  }
  return hash;
}
static uint8_t check_dead(void) {
  uint8_t status=255u;uint16_t tries=0u,before=bitmap_hash();
  world_dead_begin();
  do {status=world_dead_step(255u,2u);dead_after();if(status==255u)vsync();}while(status==255u && ++tries<2000u);
  if(status==255u)result[3]=5u;
  if(before!=bitmap_hash())result[3]=6u;
  return status;
}
static uint8_t negative(uint8_t which) {
  uint8_t k,x,y,u,v;eg_land_node *n;
  if(which>=20u) {
    uint16_t seed;
    for(seed=0u;seed<1024u;seed++)if(eg_land_plan(&world_plan,seed,0u,1u,0u) && world_plan.width==24u && world_plan.nnode==1u)break;
    world_begin(seed,0u,1u,0u);pocket_n=0u;n=&world_plan.node[0];
    for(k=0u;k<world_plan.nitem;k++)(void)world_write(world_plan.item[k].u,world_plan.item[k].v,T_EMPTY);
    (void)world_write(n->at-3u,n->lane+1u,T_EMPTY);
    for(k=0u;k<27u;k++) {
      uint16_t address=(uint16_t)(0x4000u+(uint16_t)k*9u);
      CRITICAL {view_get9(address,world_mask_packet);}
      world_mask_packet[8]=0u;
      CRITICAL {view_put9(address,world_mask_packet);}
    }
    x=which==20u ? 3u : 1u;y=eg_land_lane(&world_plan,3u);u=eg_view_local(&world_view,x,y);
    if(u>=EG_N)result[3]=8u;else room.c[u]=T_BLOCK;
    for(k=0u;k<14u;k++)world_cache_load(0,16u,world_view.y+k,eg_window_packet);
    if(!world_shift_data(4,0))result[3]=9u;
    return check_dead();
  }
  world_begin(17u,0u,0u,0u);pocket_n=0u;n=&world_plan.node[0];
  for(k=0u;k<world_plan.nitem;k++) {
    u=world_plan.item[k].u;v=world_plan.item[k].v;(void)world_write(EL_X(&world_plan,u,v),EL_Y(&world_plan,u,v),T_EMPTY);
  }
  (void)world_write(n->at-3u,n->lane+1u,T_EMPTY);
  x=n->at-3u;y=n->lane-1u;
  if(which<8u) {
    if(which==1u || which==6u || which==7u)(void)world_write(n->at-3u,n->lane+1u,T_BLOCK);
    if(which==2u)(void)world_write(1u,n->lane,T_BLOCK);
    if(which==3u || which==7u) {pocket[0]=P_BRICK;pocket_n=1u;}
    if(which==4u) {pocket[0]=P_FIRE;pocket[1]=P_MUD;pocket_n=2u;}
    if(which==5u)n->condition=EL_LATCH;
    if(which>=6u) {n->condition=EL_BOTH;(void)world_write(n->at-4u,n->lane+2u,T_PLATE);}
  } else if(which<12u) {
    n->kind=EL_HEAT;(void)world_write(n->at,n->lane,T_PAPER);(void)world_write(x,y,T_EMPTY);
    if(which==9u || which==10u) {pocket[0]=which==9u ? P_STEAM : P_LAVA;pocket_n=1u;}
    if(which==11u) {pocket[0]=P_BRICK;pocket_n=1u;}
  } else if(which<16u) {
    n->kind=EL_POWER;(void)world_write(x,y,T_SOCKET);
    if(which==13u) {pocket[0]=P_AIR;pocket[1]=P_FIRE;pocket[2]=P_METAL;pocket_n=3u;}
    if(which==14u) {pocket[0]=P_ENERGY;pocket[1]=P_METAL;pocket_n=2u;}
    if(which==15u) {pocket[0]=P_METAL;pocket_n=1u;}
  } else {
    n->kind=EL_COLD;n->alternatives=0u;(void)world_write(n->at,n->lane,T_WATER);(void)world_write(x,y,T_EMPTY);
    if(which==17u || which==18u) {pocket[0]=P_AIR;pocket[1]=P_WATER;pocket_n=2u;if(which==18u)pocket[pocket_n++]=P_AIR;}
    if(which==19u) {pocket[0]=P_AIR;pocket[1]=P_RAIN;pocket_n=2u;}
  }
  world_bind();return check_dead();
}
void main(void) {
  uint8_t which,step,i;
  DISPLAY_OFF;cpu_fast();SVBK_REG=1u;memset(result,0,4u);
  if((uint16_t)world_mask_packet>0xCFF7u || (uint16_t)eg_window_packet>0xCFF7u)result[3]=4u;
  for(i=0u;i<32u;i++)canary[i]=0xA7u;
  retained_certificates();
  for(which=0u;which<LC_CASES;which++) {
    if(!wg_begin(which))result[3]=1u;wg_direction=0u;
    for(step=0u;step<WG_STEPS;step++) {
      uint8_t code=wg_step(which,step);wg_capture(code,step,output);
      if(check_dead())result[3]=7u;
      if((SVBK_REG&7u)!=1u)result[3]=2u;
      for(i=0u;i<32u;i++)if(canary[i]!=0xA7u)result[3]=3u;
      result[1]=which;result[2]=step;result[0]=0x5Au;
      while(!(joypad()&J_A))vsync();
      result[0]=0u;while(joypad()&J_A)vsync();
    }
  }
  for(which=0u;which<22u;which++) {
    output[0]=negative(which);result[1]=which;result[2]=0u;result[0]=0x5Cu;
    while(!(joypad()&J_A))vsync();result[0]=0u;while(joypad()&J_A)vsync();
  }
  result[0]=0xA5u;while(1)vsync();
}
