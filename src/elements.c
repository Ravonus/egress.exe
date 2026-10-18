/* Nine-bit crafted effects share the canonical room/combat primitives. Bombs
 * reuse the eight orb slots; only one terrain reaction is evaluated per tick.
 * No actor allocation, new OAM pool, or per-frame art composition. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "play.h"
#include "puzzle.h"
#include "elements.h"
#include "companion.h"
#include "core/eg_health.h"
#include "core/eg_move.h"
#include "stamina.h"
#include "mob.h"
#include "look.h"
#include "oam.h"
#include "resume.h"
#include "narrator.h"
#include "world.h"
#include "gen/world_art.h"
#include "core/eg_environment.h"
extern eg_mover_t pmv;
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;
uint8_t __at(0xDDEE) elemental_active;
uint8_t __at(0xDDEF) elemental_ward;
static const int8_t dx[8]={0,1,1,1,0,-1,-1,-1},dy[8]={-1,-1,0,1,1,1,0,-1};
static uint8_t kind(uint8_t p){return EG_PIECE_HOT(p) ? FX_HOT : p==P_WATER || p==P_RAIN || p==P_CLOUD ? FX_WET : p==P_ICE || p==P_SNOW ? FX_COLD : p==P_ELECTRIC ? FX_SHOCK : p==P_BRICK || p==P_EARTH || p==P_MUD ? FX_STONE : FX_MAGIC;}
/* Environmental fields share the eight orb records. live=4 stores global
 * cell coordinates, so a camera transfer cannot cancel or move a reaction. */
#include "environment.inc"
static uint8_t clear_line(uint8_t ax,uint8_t ay,uint8_t bx,uint8_t by){
 int8_t vx,vy;uint8_t k,t;int16_t x=ax,y=ay;
 eg_aim((int16_t)bx-ax,(int16_t)by-ay,8u,&vx,&vy);
 for(k=0u;k<20u;k++){
  if(absdiff((uint8_t)x,bx)<8u && absdiff((uint8_t)y,by)<8u)return 1u;
  x+=vx;y+=vy;if((uint16_t)x>255u || (uint16_t)y>223u)return 0u;
  if(absdiff((uint8_t)x,bx)<8u && absdiff((uint8_t)y,by)<8u)return 1u;
  t=cell_type(cell_at((uint8_t)x,(uint8_t)y));if(!EG_PASSABLE(t) && t!=T_PIT && t!=T_WATER)return 0u;
 }return 0u;
}
static uint8_t terrain(uint8_t p,uint8_t c){
 uint8_t occ[5],rx,k,n=4u;int16_t x,y;
 int8_t env=elements_environment(0u,p,c,0u);if(env>=0)return (uint8_t)env;
 occ[0]=cell_at(pl_x-6u,pl_y-6u);occ[1]=cell_at(pl_x+5u,pl_y-6u);occ[2]=cell_at(pl_x-6u,pl_y+5u);occ[3]=cell_at(pl_x+5u,pl_y+5u);
 if(p==P_BRICK && companion.hp){x=(int16_t)pl_x+companion.ox;y=(int16_t)pl_y+companion.oy;if((uint16_t)x<256u && (uint16_t)y<224u)occ[n++]=cell_at((uint8_t)x,(uint8_t)y);}
 rx=eg_apply(&room,p,c,occ,n);if(!rx)return 0u;last_rx=rx;n=eg_ndirty;
 if(n>EG_DIRTY_MAX){for(k=0u;k<EG_N;k++)queue_cell(k);}else for(k=0u;k<n;k++)queue_cell(eg_dirty[k]);
 puzzle_dirty=grav_t=1u;fx_burst((uint8_t)(EG_X(c)*16u+8u),(uint8_t)(EG_Y(c)*16u+8u),kind(p),3u);return 1u;
}
static uint8_t hit(uint8_t k,uint8_t effect,uint8_t power,uint8_t x,uint8_t y){
 uint8_t ok;
 if(en[k].kind==SPAWN_BOSS){hit_src=HS_ELEMENT;hit_tags=eg_piece_tags(effect);hit_result=HR_MISS;enemy_hit(k,power,x,y);return hit_result>=HR_HURT;}
 ok=mob_element(k,effect,effect==P_ELECTRIC ? HS_FUSE : HS_ELEMENT,x,y,power);
 if(ok && EG_PIECE_HOT(effect) && en[k].st!=ES_DEAD){mob[k].status|=MS_BURN;mob[k].st_t=128u;}return ok;
}
uint8_t elements_target(uint8_t effect,uint8_t cell,uint8_t power,uint8_t radius,uint8_t x,uint8_t y) BANKED {
 uint8_t k,n=0u,ok=0u,ex,ey;uint16_t range=(uint16_t)(16u+radius*12u);
 for(k=0u;k<nen && n<3u;k++){
  if((en[k].kind!=SPAWN_MOB && en[k].kind!=SPAWN_BOSS) || en[k].st==ES_DEAD)continue;ex=(uint8_t)(en[k].x>>4);ey=(uint8_t)(en[k].y>>4);
  if((uint16_t)absdiff(x,ex)+absdiff(y,ey)>range || !clear_line(pl_x,pl_y,ex,ey))continue;
  if(hit(k,effect,power,pl_x,pl_y)){ok=1u;n++;}
 }
 if(!ok && cell<EG_N){int8_t env=elements_environment(0u,effect,cell,radius);ok=env>=0 ? (uint8_t)env : terrain(effect,cell);}return ok;
}
void elements_use(void) BANKED {
 eg_item_profile p;uint8_t slot,k,ok=0u,range,c,who;int16_t x,y;orb_t *o;
 eg_catalogue_profile(tool_piece,&p);if(!p.family)return;
 for(slot=0u;slot<pocket_n;slot++)if(pocket_id(slot)==tool_piece)break;
 if(slot==pocket_n){sfx(SFX_DENY);hud_msg("EMPTY: EQUIP AN ITEM");return;}
 if(stamina.value<p.cost){sfx(SFX_DENY);hud_msg("REST: LOW STAMINA");return;}
 if(p.kind==CK_BOMB){
  for(k=0u;k<MAXO;k++)if(!orbs[k].live)break;
  if(k==MAXO){sfx(SFX_DENY);hud_msg("WAIT: FIELD IS FULL");return;}
  o=&orbs[k];o->live=2u;o->friendly=(uint8_t)(128u|p.effect);o->pad=0u;o->life=56u;o->x=psx;o->y=psy;
  o->vx=dx[pmv.facing]*(pmv.facing&1u ? 17 : 24);o->vy=dy[pmv.facing]*(pmv.facing&1u ? 17 : 24);elemental_active|=look_bits[k];ok=1u;
 }else if(p.family==20u){
  x=(int16_t)pl_x+dx[pmv.facing]*16;y=(int16_t)pl_y+dy[pmv.facing]*16;
  if((uint16_t)x<256u && (uint16_t)y<224u){
   int8_t env;c=cell_at((uint8_t)x,(uint8_t)y);env=elements_environment(p.family,p.effect,c,0u);
   ok=env>=0 ? (uint8_t)env : elements_target(p.effect,c,p.power,p.radius,(uint8_t)x,(uint8_t)y);
  }
 }else if(p.family==18u){
  who=eg_health_pickup(&pl_hp,pl_hp_max,&pl_quarter,&companion,(uint8_t)(2u+p.power));
  if(who){if(who==2u)pocket_pet_event(pet_instance,&companion,0u);else resume_damage();ok=1u;fx_burst(pl_x,pl_y,FX_ALIVE,4u);}
 }else if(p.shape==CS_WARD || p.family==17u || (p.kind==CK_TOOL && (p.family==5u || p.family==24u))){
  if(elemental_ward<90u){elemental_ward=120u;ok=1u;fx_burst(pl_x,pl_y,FX_SHINY,4u);}
 }else {
  range=(uint8_t)(p.shape==CS_LINE || p.kind==CK_TOOL ? 3u : 1u);
  for(k=1u;k<=range && !ok;k++){
   x=(int16_t)pl_x+dx[pmv.facing]*(k*16u);y=(int16_t)pl_y+dy[pmv.facing]*(k*16u);
   if((uint16_t)x>255u || (uint16_t)y>223u)break;c=cell_at((uint8_t)x,(uint8_t)y);
   ok=elements_target(p.effect,c,p.power,p.radius,(uint8_t)x,(uint8_t)y);
   if(!ok && cell_type(c)!=T_PIT && cell_type(c)!=T_WATER && !EG_PASSABLE(cell_type(c)))break;
  }
 }
 if(!ok){sfx(SFX_DENY);hud_msg("NO EFFECT: KEPT");return;}
 player_tool_cast();
 if(p.kind!=CK_TOOL)pocket_take(slot);
 eg_stamina_spend(&stamina,p.cost);tool_cd=p.kind==CK_TOOL ? 70u : 28u;hud_dirty=puzzle_dirty=1u;
 narrator_event(NE_USE,p.effect);sfx(p.kind==CK_BOMB ? SFX_TELL : kind(p.effect)==FX_SHOCK ? SFX_ZAP : SFX_CHARGED);
 fx_burst(pl_x,pl_y,kind(p.effect),2u);hud_msg(p.kind==CK_BOMB ? "FUSE LIT: STAND BACK" : p.kind==CK_TOOL ? "TOOL: RECHARGING" : "CRAFTED ELEMENT USED");
}
void elements_tick(void) BANKED {
 uint8_t k,j,scan,c,effect,worked=0u,x,y,ex,ey,t;int16_t xx,yy,cx,cy;orb_t *o;
 if(elemental_ward){elemental_ward--;if(!(frame_count&31u))fx_burst(pl_x,pl_y,FX_SHINY,1u);}
 if(!elemental_active)return;
 /* Rotate the one-transition allowance. A blocked growing tree must not
  * starve a later flask or environmental field in the shared pool. */
 for(scan=0u;scan<MAXO;scan++){
  k=(uint8_t)((scan+(uint8_t)frame_count)&7u);
  if(!(elemental_active&look_bits[k]))continue;o=&orbs[k];
  if(!o->live || !(o->friendly&128u)){elemental_active&=(uint8_t)~look_bits[k];puzzle_dirty=1u;effect_n=128u;continue;}
  if(o->live==4u){if(!worked){environment_step(o);worked=1u;}continue;}
  effect=o->friendly&31u;x=(uint8_t)(o->x>>4);y=(uint8_t)(o->y>>4);
  if(o->live==2u){
   if(o->life>32u){
    xx=(int16_t)o->x+o->vx;yy=(int16_t)o->y+o->vy;
    if((uint16_t)xx<4096u && (uint16_t)yy<3584u){t=cell_type(cell_at((uint8_t)(xx>>4),(uint8_t)(yy>>4)));if(EG_PASSABLE(t) || t==T_PIT || t==T_WATER){o->x=xx;o->y=yy;}else o->vx=o->vy=0;}
    else o->vx=o->vy=0;
   }
   if(!(o->life&7u))fx_burst(x,(uint8_t)(y-6u),kind(effect),1u);
   if(--o->life)continue;
   o->live=3u;o->vx=o->vy=-2;o->life=25u;sfx(SFX_BREAK);shake=4u;fx_burst(x,y,kind(effect),5u);
  }
  if(worked)continue;worked=1u;cx=(int16_t)(x>>4)+o->vx;cy=(int16_t)(y>>4)+o->vy;
  if((uint16_t)cx<EG_W && (uint16_t)cy<EG_H && (uint8_t)(o->vx*o->vx+o->vy*o->vy)<=5u){
   c=EG_IDX((uint8_t)cx,(uint8_t)cy);xx=cx*16+8;yy=cy*16+8;
   /* The centre projectile may reach the other side of a wall; the blast may
    * not. Apertures made by an earlier slice become physically usable. */
   if(clear_line(x,y,(uint8_t)xx,(uint8_t)yy) || cell_at(x,y)==c){
    for(j=0u;j<nen;j++){
     if(o->pad&look_bits[j] || (en[j].kind!=SPAWN_MOB && en[j].kind!=SPAWN_BOSS) || en[j].st==ES_DEAD)continue;
     ex=(uint8_t)(en[j].x>>4);ey=(uint8_t)(en[j].y>>4);
     if(overlap((uint8_t)xx,(uint8_t)yy,ex,ey,12u,12u) && clear_line(x,y,ex,ey)){o->pad|=look_bits[j];hit(j,effect,3u,x,y);}
    }
    if(cell_type(c)==T_BRICK){player_break_cell(c,1u);fx_burst((uint8_t)xx,(uint8_t)yy,FX_STONE,2u);}
    else terrain(effect,c);
   }
  }
  if(++o->vx>2){o->vx=-2;o->vy++;}
  if(!--o->life){o->live=0u;elemental_active&=(uint8_t)~look_bits[k];puzzle_dirty=1u;effect_n=128u;}
 }
}
void elements_draw(void) BANKED {
 uint8_t k;int16_t x,y,z;orb_t *o;
 if(!elemental_active)return;
 for(k=0u;k<MAXO;k++)if((elemental_active&look_bits[k]) && orbs[k].live==2u){
  o=&orbs[k];x=(int16_t)(o->x>>4)-cam_x-4;y=(int16_t)(o->y>>4)-cam_y-8;
  z=o->life>44u ? 56u-o->life : o->life>32u ? o->life-32u : 0;
  oam_group(x,y-z,1u,(uint8_t)(ST_BOMB+((frame_count>>3)&1u)*2u),0u,5u);
 }
}
uint8_t elements_protect(uint8_t damage) BANKED {
 if(!elemental_ward)return damage;
 elemental_ward=elemental_ward>30u ? elemental_ward-30u : 0u;fx_burst(pl_x,pl_y,FX_SHINY,3u);sfx(SFX_B_CLINK);return damage ? damage-1u : 0u;
}
