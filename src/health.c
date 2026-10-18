#pragma bank 255
#include <gb/gb.h>
#include "play.h"
#include "puzzle.h"
#include "health.h"
#include "companion.h"
#include "core/eg_health.h"
#include "look.h"
#include "oam.h"
#include "resume.h"
#include "mob.h"
#include "world.h"
uint8_t health_cell[4];
#include "health_scan.inc"
void health_reset(void) BANKED {
 uint8_t i,n=0u;for(i=0u;i<4u;i++)health_cell[i]=255u;
 for(i=0u;i<EG_N && n<4u;i++)if(EG_HEALTH(room.c[i]))health_cell[n++]=i;
}
void health_shift_view(int8_t dx,int8_t dy) BANKED {
 uint8_t k,c,at,n=0u,start=dx ? (dx>0 ? 12u : 0u) : (dy>0 ? 160u : 0u);
 uint8_t mask=dx ? 15u : 255u,lo=dx ? (dx>0 ? 4u : 0u) : (dy>0 ? 64u : 0u);
 uint8_t hi=dx ? (dx>0 ? 16u : 12u) : (dy>0 ? 224u : 160u),delta=dx ? (uint8_t)dx : (uint8_t)(dy*16);
 if(!dx && !dy)return;
 for(k=0u;k<4u;k++){
  c=health_cell[k];if(c>=EG_N)continue;
  /* The view moves exactly four cells on one axis. Retained medicine uses
   * the same bounded coordinate transform without four far helper calls. */
  at=c&mask;if(at<lo || at>=hi)continue;c=(uint8_t)(c-delta);
  if(c<EG_N && EG_HEALTH(room.c[c]))health_cell[n++]=c;
 }
 for(k=n;k<4u;k++)health_cell[k]=255u;
 if(n<4u)incoming(room.c+start,start,dx ? 142u : 16u,n);
}
void health_load(void) BANKED {health_art_load();health_reset();}
uint8_t health_pick(uint8_t cell) BANKED {
 uint8_t who;if(cell>=EG_N || !EG_HEALTH(room.c[cell]))return 0u;
 who=eg_health_pickup(&pl_hp,pl_hp_max,&pl_quarter,&companion,4u);if(!who)return 0u;
 room.c[cell]=T_EMPTY;queue_cell(cell);hud_dirty=1u;
 if(who==2u)pocket_pet_event(pet_instance,&companion,0u);
 else resume_damage();
 fx_burst(pl_x,pl_y,FX_ALIVE,4u);sfx(SFX_KEY);hud_msg(who==2u ? "CRITTER MENDED" : "HEALTH RESTORED");return 1u;
}
uint8_t health_drop(uint8_t cell) BANKED {
 uint8_t k;if(world_on || cell>=EG_N || room.c[cell]!=T_EMPTY || EG_PROT(&room,cell))return 0u;
 for(k=0u;k<4u;k++)if(health_cell[k]>=EG_N || !EG_HEALTH(room.c[health_cell[k]]))break;
 if(k==4u)return 0u;health_cell[k]=cell;room.c[cell]=T_TOKEN|F_HEALTH;queue_cell(cell);return 1u;
}
void health_draw(void) BANKED {
 uint8_t k,c,tile=(uint8_t)(ST_HEALTH+(((uint8_t)(frame_count>>4))&1u)*4u);
 if((health_cell[0]&health_cell[1]&health_cell[2]&health_cell[3])==255u)return;
 for(k=0u;k<4u;k++){
  c=health_cell[k];if(c>=EG_N || !EG_HEALTH(room.c[c]) || !look_here(c))continue;
  oam_group((int16_t)EG_X(c)*16-cam_x,(int16_t)EG_Y(c)*16-cam_y-2,2u,tile,2u,5u);
 }
}
