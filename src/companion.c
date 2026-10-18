/* One active, finite-life critter. No actor/projectile allocation of its own:
 * two double-buffered OBJs, the existing eight-particle pool, one decision per
 * eight simulation ticks, and the same conditional mob damage rules. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "companion.h"
#include "catalogue_art.h"
#include "core/eg_health.h"
#include "core/eg_move.h"
#include "stamina.h"
#include "mob.h"
#include "look.h"
#include "oam.h"
#include "resume.h"
#include "mapper.h"
#include "save_map.h"
extern eg_mover_t pmv;
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;
extern uint8_t world_on,world_moved;
#define PET_STEERING ((uint8_t *)0xBF00u)
typedef char pet_steering_fits[(SAVE_STATS_B+SAVE_STATS_BYTES==0xBF00u && 0xBF00u<0xBFFFu) ? 1 : -1];
eg_companion __at(0xDDDC) companion;
uint8_t __at(0xDDD9) pet_instance;
static uint8_t __at(0xDDEC) previous_x;
static uint8_t __at(0xDDED) previous_y;
static const int8_t dirx[8]={0,1,1,1,0,-1,-1,-1},diry[8]={-1,-1,0,1,1,1,0,-1};
typedef char companion_size[(sizeof(eg_companion)==16u && 0xDDDCu+16u==0xDDECu && 0xDDF0u+EG_PET_BAG_BYTES==0xDE38u) ? 1 : -1];

static uint8_t px(void){int16_t x=(int16_t)pl_x+companion.ox;return x<0 ? 0u : x>255 ? 255u : (uint8_t)x;}
static uint8_t py(void){int16_t y=(int16_t)pl_y+companion.oy;return y<0 ? 0u : y>223 ? 223u : (uint8_t)y;}
static uint8_t visible(uint8_t x,uint8_t y){return x>=cam_x && (uint16_t)x<cam_x+160u && y>=cam_y && (uint16_t)y<cam_y+128u;}
static uint8_t line(uint8_t x,uint8_t y,uint8_t tx,uint8_t ty){
 int8_t dx,dy,sx,sy,error,twice;uint8_t k,t;
 x>>=4;y>>=4;tx>>=4;ty>>=4;
 dx=(int8_t)absdiff(x,tx);dy=-(int8_t)absdiff(y,ty);sx=x<tx ? 1 : -1;sy=y<ty ? 1 : -1;error=dx+dy;
 /* Walk each crossed map cell once; pixel stepping repeated the same tile
  * and paid for a normalized projectile vector on every target probe. */
 for(k=0u;k<16u;k++){
  if(x==tx && y==ty)return 1u;
  twice=error*2;
  if(twice>=dy){error+=dy;x+=sx;}
  if(twice<=dx){error+=dx;y+=sy;}
  if(x>=16u || y>=14u)return 0u;t=cell_type(EG_IDX(x,y));
  if(!EG_PASSABLE(t) && t!=T_PIT && t!=T_WATER)return 0u;
 }
 return 0u;
}
static uint8_t floor_ok(uint8_t x,uint8_t y){
 uint8_t c,t,left,right,top,bottom,col,row,fly=(uint8_t)(companion.id==434u || companion.id==435u || companion.id==438u || companion.id==442u || companion.id==444u);
 if(x<4u || x>251u || y<4u || y>219u)return 0u;
 left=(x-3u)>>4;right=(x+3u)>>4;top=(y-3u)&240u;bottom=(y+3u)&240u;
 /* The four footprint corners usually share one cell. Check each distinct
  * covered cell once, retaining the same feet/hidden-floor collision rule. */
 row=top;
 while(1){
  col=left;
  while(1){
   c=row|col;t=cell_type(c);
   if((roam_hidden[c>>3]&look_bits[c&7u]) || (room.c[c]&F_OB))return 0u;
   if(!EG_PASSABLE(t) && !((fly || pl_air) && (t==T_PIT || t==T_WATER)))return 0u;
   if(col==right)break;col=right;
  }
  if(row==bottom)break;row=bottom;
 }
 return 1u;
}
static uint8_t target(uint8_t range,uint8_t forward){
 uint8_t k,x,y,d,best=255u,score=255u;int16_t dx,dy;uint16_t sum;
 for(k=0u;k<nen;k++){
  if(en[k].st==ES_DEAD || (en[k].kind==SPAWN_MOB && (mob[k].status&MS_UNDER)))continue;
  x=(uint8_t)(en[k].x>>4);y=(uint8_t)(en[k].y>>4);if(!visible(x,y))continue;
  sum=(uint16_t)absdiff(px(),x)+absdiff(py(),y);if(sum>range)continue;d=(uint8_t)sum;
  if(forward){dx=(int16_t)x-pl_x;dy=(int16_t)y-pl_y;if(dx*dirx[pmv.facing]+dy*diry[pmv.facing]<0)continue;}
  if(d<score && line(px(),py(),x,y)){score=d;best=k;}
 }
 return best;
}
static void heal_pose(void){companion.state=(companion.state&248u)|PP_HEAL;companion.timer=24u;fx_burst(px(),py(),FX_ALIVE,3u);}
static uint8_t heal(uint8_t amount){
 uint8_t who=eg_health_pickup(&pl_hp,pl_hp_max,&pl_quarter,&companion,amount);
 if(who){hud_dirty=1u;if(who==2u)pocket_pet_event(pet_instance,&companion,0u);else pet_save_dirty|=1u;heal_pose();}
 return who;
}
static uint8_t taunt(uint8_t range,uint8_t all){
 uint8_t k,x,y,mask=0u;uint16_t d;
 for(k=0u;k<nen;k++){
  if(en[k].kind!=SPAWN_MOB || en[k].st==ES_DEAD || (mob[k].status&MS_UNDER))continue;
  x=(uint8_t)(en[k].x>>4);y=(uint8_t)(en[k].y>>4);d=(uint16_t)absdiff(px(),x)+absdiff(py(),y);
  if(d<=range && visible(x,y) && line(px(),py(),x,y)){mask|=look_bits[k];fx_burst(x,y,FX_MAGIC,1u);if(!all)break;}
 }
 if(!mask)return 0u;companion.threat|=mask;companion.threat_t=25u;return 1u;
}
static uint8_t strike(uint8_t k,uint8_t piece,uint8_t damage){
 uint8_t ok=0u;
 if(k>=nen || en[k].st==ES_DEAD)return 0u;
 if(piece && en[k].kind==SPAWN_MOB)ok=mob_element(k,piece,HS_PET,px(),py(),1u);
 else {hit_src=HS_PET;hit_tags=0u;hit_result=HR_MISS;enemy_hit(k,damage,px(),py());ok=(uint8_t)(en[k].kind!=SPAWN_MOB || hit_result>=HR_HURT);}
 return ok;
}
static uint8_t needs_target(uint8_t action){
 return (uint8_t)(action!=PA_HEAL && action!=PA_CLEANSE && action!=PA_HASTE && action!=PA_TAUNT && action!=PA_BRACE && action!=PA_GUARD && action!=PA_WARD && action!=PA_REGEN && action!=PA_REFLECT);
}
static uint8_t perform(uint8_t action,uint8_t k,uint8_t manual,const eg_pet_profile *p){
 uint8_t j,x,y,ok=0u;
 switch(action){
  case PA_HEAL:return heal(manual ? 2u : 1u);
  case PA_CLEANSE:
   ok=heal(2u);if(!(companion.buff&PB_WARD)){companion.buff|=PB_WARD;companion.buff_t=60u;companion.guard=2u;ok=1u;}return ok;
  case PA_REGEN:
   if(companion.buff&PB_REGEN)return 0u;companion.buff|=PB_REGEN;companion.buff_t=60u;heal(1u);return 1u;
  case PA_HASTE:
   if(companion.buff&PB_HASTE)return 0u;companion.buff|=PB_HASTE;companion.buff_t=60u;return 1u;
  case PA_WARD:case PA_GUARD:case PA_REFLECT:
   if(companion.guard>=4u && companion.buff_t)return 0u;
   companion.guard=action==PA_GUARD ? 6u : 4u;companion.buff|=PB_WARD;
   if(action==PA_REFLECT)companion.buff|=PB_REFLECT;
   companion.buff_t=75u;companion.state=(companion.state&248u)|PP_GUARD;return 1u;
  case PA_BRACE:
   companion.guard=6u;companion.buff|=PB_MIGHT;companion.buff_t=80u;taunt(p->range,1u);companion.state=(companion.state&248u)|PP_GUARD;return 1u;
  case PA_TAUNT:
   if(!manual && companion.hp<=p->hp/3u)return 0u;
   if(!taunt(p->range,manual))return 0u;
   if(companion.guard<2u)companion.guard=2u;companion.buff_t=60u;return 1u;
 }
 if(k>=nen)return 0u;
 x=(uint8_t)(en[k].x>>4);y=(uint8_t)(en[k].y>>4);
 switch(action){
  case PA_STRIKE:return strike(k,0u,(uint8_t)(1u+!!(companion.buff&PB_MIGHT)));
  case PA_BURN:case PA_BURN_FIELD:
   ok=strike(k,P_FIRE,1u);
   if(ok && en[k].kind==SPAWN_MOB && en[k].st!=ES_DEAD){mob[k].status|=MS_BURN;mob[k].st_t=128u;}
   if(action==PA_BURN_FIELD)for(j=0u;j<nen;j++){
    if(j==k || en[j].st==ES_DEAD || !overlap(x,y,(uint8_t)(en[j].x>>4),(uint8_t)(en[j].y>>4),25u,25u) || !line(px(),py(),(uint8_t)(en[j].x>>4),(uint8_t)(en[j].y>>4)))continue;
    if(strike(j,P_FIRE,1u) && en[j].kind==SPAWN_MOB && en[j].st!=ES_DEAD){mob[j].status|=MS_BURN;mob[j].st_t=128u;ok=1u;}break;
   }return ok;
  case PA_PUSH:return strike(k,P_AIR,1u);
  case PA_SHOCK:case PA_CHAIN:
   ok=strike(k,P_ELECTRIC,1u);
   if(action==PA_CHAIN)for(j=0u;j<nen;j++)if(j!=k && en[j].st!=ES_DEAD && overlap(x,y,(uint8_t)(en[j].x>>4),(uint8_t)(en[j].y>>4),32u,32u) && line(x,y,(uint8_t)(en[j].x>>4),(uint8_t)(en[j].y>>4))){ok|=strike(j,P_ELECTRIC,1u);break;}
   return ok;
  case PA_SLOW:case PA_FREEZE:case PA_BLIND:case PA_ROOT:case PA_EXPOSE:
   if(en[k].kind!=SPAWN_MOB)return strike(k,action==PA_FREEZE ? P_ICE : P_DUST,1u);
   if(action==PA_EXPOSE)mob[k].flags|=MOBF_EXPOSE;
   else mob[k].status|=action==PA_SLOW ? MS_COLD : action==PA_FREEZE ? MS_FROZEN : action==PA_BLIND ? MS_BLIND : MS_STUN;
   if(mob[k].st_t<90u)mob[k].st_t=90u;
   if(action==PA_ROOT){companion.buff|=PB_REGEN;companion.buff_t=60u;}
   fx_burst(x,y,action==PA_FREEZE || action==PA_SLOW ? FX_COLD : action==PA_ROOT ? FX_ALIVE : FX_MAGIC,3u);return 1u;
 }
 return 0u;
}
static uint8_t puzzle_command(void){
 uint8_t c,rx,n,k,occ[4],f;int16_t x=(int16_t)EG_X(cell_at(pl_x,pl_y))+dirx[pmv.facing],y=(int16_t)EG_Y(cell_at(pl_x,pl_y))+diry[pmv.facing];eg_item_profile profile;
 if((uint16_t)x>=EG_W || (uint16_t)y>=EG_H)return 0u;
 eg_catalogue_profile(companion.id,&profile);f=profile.effect;c=EG_IDX((uint8_t)x,(uint8_t)y);
 occ[0]=cell_at(pl_x-6u,pl_y-6u);occ[1]=cell_at(pl_x+5u,pl_y-6u);occ[2]=cell_at(pl_x-6u,pl_y+5u);occ[3]=cell_at(pl_x+5u,pl_y+5u);
 rx=eg_apply(&room,f,c,occ,4u);if(!rx)return 0u;last_rx=rx;n=eg_ndirty;
 if(n>EG_DIRTY_MAX){for(k=0u;k<EG_N;k++)queue_cell(k);}else for(k=0u;k<n;k++)queue_cell(eg_dirty[k]);
 puzzle_dirty=grav_t=1u;fx_burst((uint8_t)(x*16+8),(uint8_t)(y*16+8),f==P_FIRE ? FX_HOT : f==P_ICE || f==P_SNOW ? FX_COLD : FX_MAGIC,3u);return 1u;
}
void companion_begin(void) BANKED {eg_pet_start(&companion,0u);pet_instance=0u;previous_x=pl_x;previous_y=pl_y;}
void companion_enter(void) BANKED {
 uint8_t *e=(uint8_t *)SAVE_PET_ENTRY;companion.threat=companion.threat_t=0u;
 EG_SRAM_OPEN();
 if(companion.id && (companion.hp || (companion.buff&PB_RISING))){memcpy(e,&companion,16u);e[16]=pet_instance;}else memset(e,0,17u);
 e[17]=pet_serial;memcpy(e+18u,pet_meta,EG_PET_BAG_BYTES);EG_SRAM_CLOSE();
}
void companion_restore(void) BANKED {EG_SRAM_OPEN();memcpy(&companion,(const uint8_t *)SAVE_PET_ENTRY,16u);pet_instance=*(uint8_t *)(SAVE_PET_ENTRY+16u);EG_SRAM_CLOSE();if(companion.hp){companion.state=(companion.state&248u)|PP_IDLE;companion.timer=0u;}companion_reset_position();}
void companion_reset_position(void) BANKED {previous_x=pl_x;previous_y=pl_y;companion.ox=-16;companion.oy=4;companion.shown=255u;EG_SRAM_OPEN();*PET_STEERING=0u;EG_SRAM_CLOSE();}
void companion_shift_view(int8_t dx,int8_t dy) BANKED {previous_x=(uint8_t)(previous_x-dx*16);previous_y=(uint8_t)(previous_y-dy*16);}
uint8_t companion_equip(uint8_t slot) BANKED {
 eg_item_id id,old=companion.id;uint8_t hp,spent,cd,instance,oldhp=companion.hp,oldspent=(uint8_t)(companion.state&PET_REVIVE_SPENT),oldcd=companion.cd>companion.auto_cd ? companion.cd : companion.auto_cd,oldinstance=pet_instance;
 if(slot>=pocket_n || (old && !oldhp))return 0u;id=pocket_id(slot);if(id<EG_PETS_FIRST)return 0u;
 hp=pet_meta[slot*3u]&PM_HP;spent=pet_meta[slot*3u]&PM_REVIVED;cd=pet_meta[slot*3u+1u];instance=pet_meta[slot*3u+2u];if(!hp || !instance)return 0u;
 pocket_take(slot);eg_pet_start(&companion,id);companion.hp=hp;if(spent)companion.state|=PET_REVIVE_SPENT;companion.cd=companion.auto_cd=cd;pet_instance=instance;
 if(old)pocket_add_pet(old,oldhp,oldspent,oldcd,oldinstance);
 tool_piece=id;companion_reset_position();sfx(SFX_KEY);fx_burst(pl_x,pl_y,FX_ALIVE,3u);hud_dirty=puzzle_dirty=1u;return 1u;
}
uint8_t companion_command(void) BANKED {
 eg_pet_profile p;uint8_t action,k,phase,ok;
 if(!companion.id || !companion.hp || pl_state!=PS_FREE || pl_air || freeze || !eg_pet_get(companion.id,&p))return 0u;
 action=eg_pet_ready(&companion,1u,stamina.value);if(!action){sfx(SFX_DENY);hud_msg(companion.cd ? "CRITTER: RECHARGING" : "CRITTER: LOW STAMINA");return 0u;}
 companion.state=(companion.state&199u)|(pmv.facing<<3);k=needs_target(action) ? target(p.range,1u) : 255u;ok=perform(action,k,1u,&p);
 if(!ok)ok=puzzle_command();if(!ok){sfx(SFX_DENY);hud_msg("CRITTER: NO TARGET");return 0u;}
 phase=companion.state&7u;eg_stamina_spend(&stamina,eg_pet_commit(&companion,1u));
 pocket_pet_event(pet_instance,&companion,0u);
 player_tool_cast();
 if(phase==PP_HEAL || phase==PP_GUARD)companion.state=(companion.state&248u)|phase;
 sfx(SFX_CHARGED);fx_burst(px(),py(),FX_MAGIC,2u);hud_dirty=1u;return 1u;
}
void companion_tick(void) BANKED {
 eg_pet_profile p;uint8_t action,k,phase,steering=0u;int16_t dx,dy,xx,yy;int8_t wantx,wanty,mx=0,my=0;
 if(!companion.id)return;
 dx=(int8_t)(pl_x-previous_x);dy=(int8_t)(pl_y-previous_y);previous_x=pl_x;previous_y=pl_y;
 if(dx>24 || dx<-24 || dy>24 || dy<-24){companion_reset_position();fx_burst(px(),py(),FX_MAGIC,2u);}
 else {companion.ox=(int8_t)(companion.ox-dx);companion.oy=(int8_t)(companion.oy-dy);}
 if(!(frame_count&7u)){
  k=(uint8_t)!companion.hp;phase=(companion.cd ? 1u : 0u)|(companion.auto_cd ? 2u : 0u)|(companion.buff_t ? 4u : 0u);eg_pet_tick(&companion);
  if(companion.hp && (((phase&1u) && !companion.cd) || ((phase&2u) && !companion.auto_cd) || ((phase&4u) && !companion.buff_t)))pocket_pet_event(pet_instance,&companion,0u);
  if(k && companion.hp){pocket_pet_event(pet_instance,&companion,0u);effect_n=128u;puzzle_dirty=hud_dirty=1u;fx_burst(px(),py(),FX_ALIVE,4u);sfx(SFX_KEY);hud_msg("SECOND SPRING");}
 }
 if(!companion.hp){
  if(companion.timer)companion.timer--;
  if(!(companion.buff&PB_RISING) && !companion.timer){
   pocket_pet_event(pet_instance,&companion,1u);if(tool_piece==companion.id)tool_piece=0u;
   fx_burst(px(),py(),FX_KILL,4u);companion.id=0u;pet_instance=0u;hud_dirty=1u;hud_msg("YOUR CRITTER DIED");
  }return;
 }
 if(pl_state==PS_FALL || pl_state==PS_EXIT){if((companion.state&7u)==PP_FOLLOW)companion.state=(companion.state&248u)|PP_IDLE;return;}
 /* Keep life/charge clocks and world-relative position exact. Following and
  * automatic decisions can resume next tick after a costly stream/save. */
 if(video_epoch!=frame_epoch || (LY_REG>=88u && LY_REG<144u))return;
 if(companion.buff&PB_RETALIATE){companion.buff&=(uint8_t)~PB_RETALIATE;k=target(24u,0u);if(k<nen)strike(k,0u,1u);}
 phase=companion.state&7u;
 if(phase>=PP_CAST && phase!=PP_REST && companion.timer){companion.timer--;if(companion.timer)return;}
 /* Quiet-world steering needs a new decision only every other tick. Feet,
  * offsets, facing, animation and collision still advance on every tick.
  * Input changes, view rebases, abilities and fights always decide afresh. */
 if(world_on && !world_moved && !enemies_alive && (frame_count&1u) &&
    (phase==PP_FOLLOW || phase==PP_IDLE || phase==PP_REST) && !((joy^joy_prev)&15u)) {
  EG_SRAM_OPEN();steering=*PET_STEERING;EG_SRAM_CLOSE();
 }
 if(steering&128u){mx=(int8_t)((steering&7u)-3u);my=(int8_t)(((steering>>3)&7u)-3u);}
 else {
  if(!eg_pet_get(companion.id,&p))return;
  wantx=(int8_t)(-dirx[pmv.facing]*16-8);wanty=(int8_t)(-diry[pmv.facing]*16+4);
  if(p.role==PR_TANK && enemies_alive && companion.hp>p.hp/3u){wantx=dirx[pmv.facing]*18;wanty=diry[pmv.facing]*18;}
  dx=(int16_t)wantx-companion.ox;dy=(int16_t)wanty-companion.oy;
  if(dx>1)mx=dx>20 ? 3 : 1;else if(dx<-1)mx=dx<-20 ? -3 : -1;
  if(dy>1)my=dy>20 ? 3 : 1;else if(dy<-1)my=dy<-20 ? -3 : -1;
  if(world_on){EG_SRAM_OPEN();*PET_STEERING=(uint8_t)(128u|(mx+3u)|((my+3u)<<3));EG_SRAM_CLOSE();}
 }
 xx=(int16_t)pl_x+companion.ox+mx;yy=(int16_t)pl_y+companion.oy+my;
 if(!mx && !my) { /* No displacement needs no repeated footprint query. */ }
 else if((uint16_t)xx<250u && (uint16_t)yy<218u && floor_ok((uint8_t)xx,(uint8_t)yy)){
  companion.ox+=mx;companion.oy+=my;
 }else if(mx && (uint16_t)xx<250u && floor_ok((uint8_t)xx,py())){companion.ox+=mx;my=0;}
 else if(my && (uint16_t)yy<218u && floor_ok(px(),(uint8_t)yy)){companion.oy+=my;mx=0;}
 else mx=my=0;
 if((companion.ox>56 || companion.ox<-56 || companion.oy>56 || companion.oy<-56) && !visible(px(),py())){companion_reset_position();fx_burst(px(),py(),FX_MAGIC,3u);}
 if(mx || my){
  k=my<0 ? (mx<0 ? 7u : mx>0 ? 1u : 0u) : my>0 ? (mx<0 ? 5u : mx>0 ? 3u : 4u) : mx<0 ? 6u : 2u;
  companion.state=(companion.state&192u)|(k<<3)|PP_FOLLOW;companion.timer=0u;
 }else {if(companion.timer<200u)companion.timer++;companion.state=(companion.state&248u)|(companion.timer>=160u ? PP_REST : PP_IDLE);}
 if(frame_count&7u)return;
 /* An automatic area/taunt probe can cost another full movement pass.
  * Defer that decision when this frame already spent its actor allowance. */
 if(LY_REG>=72u && LY_REG<144u)return;
 if(companion.buff_t && !(frame_count&31u))fx_burst(pl_x,(uint8_t)(pl_y-6u),(companion.buff&PB_REGEN) ? FX_ALIVE : FX_MAGIC,1u);
 if((companion.buff&PB_REGEN) && !(frame_count&127u))heal(1u);
 if(stamina.value<(uint8_t)(28u+p.auto_cost))return;
 action=eg_pet_ready(&companion,0u,stamina.value);if(!action)return;
 if(needs_target(action)){k=target(p.range,0u);if(k==255u)return;}
 else {k=255u;if(!enemies_alive && action!=PA_HEAL && action!=PA_REGEN)return;}
 if(perform(action,k,0u,&p)){
  phase=companion.state&7u;eg_stamina_spend(&stamina,eg_pet_commit(&companion,0u));
  pocket_pet_event(pet_instance,&companion,0u);
  if(phase==PP_HEAL || phase==PP_GUARD)companion.state=(companion.state&248u)|phase;
  fx_burst(px(),py(),p.role==PR_HEAL ? FX_ALIVE : FX_MAGIC,2u);
 }
}
void companion_art_tick(void) BANKED {
 uint8_t state,dir,view,pose,key,tile;
 if(!companion.id || ((LCDC_REG&LCDCF_ON) && (video_epoch!=frame_epoch || (LY_REG>=16u && LY_REG<144u))))return;
 state=companion.state&7u;dir=(companion.state>>3)&7u;view=dir>4u ? 8u-dir : dir;
 pose=state==PP_FOLLOW ? (uint8_t)((frame_count>>3)&3u) : state==PP_IDLE || state==PP_REST ? (uint8_t)((frame_count>>5)&3u) : (uint8_t)((24u-(companion.timer>24u ? 24u : companion.timer))/6u);
 if(pose>3u)pose=3u;key=(uint8_t)(view*32u+state*4u+pose);if(key==companion.shown)return;
 tile=(uint8_t)(ST_COMPANION+((companion.state&64u) ? 0u : 4u));
 VBK_REG=0u;catalogue_art_upload(companion.id,(uint8_t)(state|(dir<<3)),pose,tile);
 if(companion.shown==255u)catalogue_art_palette(companion.id,4u,1u);
 companion.state^=64u;companion.shown=key;
}
void companion_draw(void) BANKED {
 uint8_t dir,tile;int16_t x=(int16_t)pl_x+companion.ox-8-cam_x,y=(int16_t)pl_y+companion.oy-10-cam_y;
 if(!companion.id || companion.shown==255u)return;
 dir=(companion.state>>3)&7u;tile=ST_COMPANION+((companion.state&64u) ? 4u : 0u);
 if(dir>4u)oam_group(x,y,2u,tile+2u,254u,4u|S_FLIPX);else oam_group(x,y,2u,tile,2u,4u);
}
uint8_t companion_aim(uint8_t enemy,uint8_t *x,uint8_t *y) BANKED {
 uint8_t ex,ey;if(!companion.hp || !companion.threat_t || enemy>=nen || !(companion.threat&look_bits[enemy]))return 0u;
 ex=(uint8_t)(en[enemy].x>>4);ey=(uint8_t)(en[enemy].y>>4);
 if((uint16_t)absdiff(px(),ex)+absdiff(py(),ey)>96u || ((frame_count&7u)==enemy && !line(ex,ey,px(),py()))){companion.threat&=(uint8_t)~look_bits[enemy];return 0u;}
 *x=px();*y=py();return 1u;
}
uint8_t companion_touch(uint8_t damage,uint8_t x,uint8_t y,uint8_t range) BANKED {
 uint8_t pending,hp=companion.hp;
 if(!companion.id || !overlap(x,y,px(),py(),range,range) || !eg_pet_hurt(&companion,damage))return 0u;
 pending=(uint8_t)(companion.buff&PB_RISING);pocket_pet_event(pet_instance,&companion,(uint8_t)(!companion.hp && !pending));
 if(!companion.hp){puzzle_dirty=1u;effect_n=128u;}
 if(companion.hp && (companion.buff&PB_MIGHT))companion.buff|=PB_RETALIATE;
 fx_burst(px(),py(),pending ? FX_ALIVE : companion.hp==hp ? FX_SHINY : FX_HIT,3u);sfx(pending ? SFX_KEY : companion.hp==hp ? SFX_B_CLINK : SFX_HURT);hud_dirty=1u;return 1u;
}
uint8_t companion_intercept(uint8_t x,uint8_t y) BANKED {
 uint8_t reflect;
 if(!companion.hp || !overlap(x,y,px(),py(),13u,13u))return 0u;
 if(companion.guard){reflect=(uint8_t)(companion.buff&PB_REFLECT);companion.guard--;pocket_pet_event(pet_instance,&companion,0u);fx_burst(px(),py(),FX_MAGIC,1u);return reflect ? 2u : 1u;}
 return companion_touch(1u,x,y,13u);
}
uint8_t companion_protect(uint8_t damage) BANKED {
 uint8_t absorb;
 if(!damage || !companion.hp || !(companion.buff&PB_WARD) || !companion.guard)return damage;
 absorb=damage<companion.guard ? damage : companion.guard;companion.guard-=absorb;
 pocket_pet_event(pet_instance,&companion,0u);
 fx_burst(pl_x,pl_y,FX_MAGIC,2u);sfx(SFX_DENY);return damage-absorb;
}
uint8_t companion_assist(void) BANKED {
 eg_item_profile p;if(!companion.hp && !(companion.buff&PB_RISING))return 0u;eg_catalogue_profile(companion.id,&p);
 return EG_PIECE_HOT(p.effect) ? EG_ASSIST_HOT : p.effect==P_ICE || p.effect==P_SNOW ? EG_ASSIST_COLD : p.effect==P_ELECTRIC ? EG_ASSIST_POWER : p.effect==P_MUD ? EG_ASSIST_MUD : 0u;
}
