/* Ahead-of-view packet warming. The active view and object pools stay fixed;
 * generated backing packets are reused by either later transfer direction. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "play.h"
#include "world.h"
#include "look.h"
#include "mapper.h"
#include "save_map.h"
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;
void eg_land_cached_at(const eg_land *p,uint8_t x,uint8_t y,eg_land_cell *out) BANKED;
typedef struct {uint8_t packet[9],index,part,x,y,ox,oy;} warm_state;
#define WARM_ADDRESS ((uint8_t *)0xB4B1u)
typedef char warm_scratch_fits[(sizeof(warm_state)==15u &&
 SAVE_FUSION_SCRATCH+SAVE_FUSION_BYTES+64u+11u==0xB4B1u &&
 0xB4B1u+sizeof(warm_state)==SAVE_CLOCK) ? 1 : -1];
static uint8_t time_left(uint8_t deadline){return video_epoch==frame_epoch && (LY_REG<deadline || LY_REG>=144u);}
static uint8_t forecast(uint8_t u,uint8_t span,uint8_t size){
 uint8_t lane=eg_land_lane(&world_plan,u),at=lane>span/2u ? (uint8_t)((lane-span/2u+2u)&252u) : 0u;
 return at>size-span ? size-span : at;
}
void world_warm_reset(void) BANKED {
 warm_state work;memset(&work,0,sizeof work);work.ox=work.oy=255u;
 EG_SRAM_OPEN();memcpy(WARM_ADDRESS,&work,sizeof work);EG_SRAM_CLOSE();
}
static void warm(uint8_t deadline) {
 uint8_t tries,limit,x,y,k,span,row;eg_land_cell cell;warm_state work;
 if(!world_on || !time_left(deadline))return;
 EG_SRAM_OPEN();memcpy(&work,WARM_ADDRESS,sizeof work);
 if(work.ox!=world_view.x || work.oy!=world_view.y){work.index=work.part=0u;work.ox=world_view.x;work.oy=world_view.y;}
 /* Warm the route's shoulders too. A bend can request an orthogonal strip
  * from the preceding columns before it reaches the next long-axis seam. */
 limit=world_plan.vertical ? 128u : 88u;
 for(tries=0u;tries<8u && work.index<limit && time_left(deadline);tries++){
  if(!work.part){
   /* Complete the nearest four-column packet first, from the walking lane
    * outward. A view rebase must not repeatedly restart distant empty rows. */
   x=(uint8_t)(world_view.x+(work.index/22u)*4u+16u);
   y=0u;
   if(world_plan.vertical){
    y=(uint8_t)(world_view.y+14u+(work.index>>3));
    if(y>=world_view.height){work.index++;continue;}
    span=world_view.width<32u ? world_view.width : 32u;
    x=(uint8_t)(forecast(y,span,world_view.width)+(work.index&7u)*4u);
   }else{
    if(x>=world_view.width){work.index++;continue;}
    span=world_view.height<22u ? world_view.height : 22u;
    row=work.index%22u;
    row=row&1u ? (uint8_t)(11u+(row>>1)) : (uint8_t)(10u-(row>>1));
    if(row>=span){work.index++;continue;}
    y=(uint8_t)(forecast((uint8_t)(x+2u),span,world_view.height)+row);
   }
   if(x>=world_view.width || y>=world_view.height || world_cached(x,y)){work.index++;continue;}
   work.x=x;work.y=y;work.packet[8]=0u;
  }
  if(!time_left(deadline))break;
  k=work.part;eg_land_cached_at(&world_plan,(uint8_t)(work.x+k),work.y,&cell);
  work.packet[k]=cell.material;work.packet[k+4u]=world_skin(cell.design,cell.variant);
  if(cell.flags&EL_PROTECTED)work.packet[8]|=look_bits[k];
  if(++work.part==4u){
   if(!world_cached(work.x,work.y)){
    memcpy(eg_window_packet,work.packet,9u);view_store(0,work.x,work.y,eg_window_packet);world_mark_cached(work.x,work.y);
   }
   work.part=0u;work.index++;
  }
 }
 memcpy(WARM_ADDRESS,&work,sizeof work);EG_SRAM_CLOSE();
}
void world_warm_tick(void) BANKED {warm(32u);}
void world_warm_end(void) BANKED {warm(84u);}
