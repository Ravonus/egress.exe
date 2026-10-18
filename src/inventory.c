#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "collection.h"
#include "core/eg_bag.h"
#include "core/eg_piece.h"
#include "companion.h"
#include "resume.h"
#include "save_map.h"
#include "mapper.h"
uint8_t __at(0xDBC0) pocket[POCKET_CAP];
uint8_t __at(0xDDD0) pocket_hi[3];
uint8_t __at(0xDDF0) pet_meta[EG_PET_BAG_BYTES];
uint8_t __at(0xDDDA) pet_serial;
static uint8_t __at(0xDBD8) entry_pocket[POCKET_CAP];
static uint8_t __at(0xDDD3) entry_hi[3];
static uint8_t __at(0xDBA4) entry_tool;
static uint8_t __at(0xDDD6) entry_tool_hi;
static uint8_t entry_n;
uint8_t pocket_n,tool_cd;
eg_item_id tool_piece;
typedef char bag_layout[(POCKET_CAP==EG_BAG_CAP && EG_BAG_HIGH_BYTES==3u) ? 1 : -1];
void pocket_begin(void) BANKED {
  effect_n=128u;
  memset(pocket,0,sizeof pocket);memset(pocket_hi,0,sizeof pocket_hi);memset(pet_meta,0,sizeof pet_meta);pet_serial=pet_save_dirty=0u;
  pocket_n=tool_cd=0u;tool_piece=0u;companion_begin();collection_load();
}
void pocket_enter(void) BANKED {
  memcpy(entry_pocket,pocket,sizeof pocket);memcpy(entry_hi,pocket_hi,3u);entry_n=pocket_n;entry_tool=(uint8_t)tool_piece;entry_tool_hi=(uint8_t)(tool_piece>>8);
  companion_enter();
}
void pocket_restore(void) BANKED {
  effect_n=128u;
  memcpy(pocket,entry_pocket,sizeof pocket);memcpy(pocket_hi,entry_hi,3u);pocket_n=entry_n;tool_piece=(eg_item_id)(entry_tool|((uint16_t)entry_tool_hi<<8));tool_cd=0u;hud_dirty=1u;
  EG_SRAM_OPEN();memcpy(pet_meta,(const uint8_t *)(SAVE_PET_ENTRY+18u),sizeof pet_meta);pet_serial=*(uint8_t *)(SAVE_PET_ENTRY+17u);EG_SRAM_CLOSE();companion_restore();
}
uint8_t pocket_add(eg_item_id id) BANKED {
  eg_pet_profile p;uint8_t instance;
  if(id>=EG_PETS_FIRST){
    if(pocket_n>=POCKET_CAP || !eg_pet_get(id,&p))return 0u;
    EG_SRAM_OPEN();instance=eg_pet_instance_next(pet_serial,pet_instance,pet_meta,pocket_n,(const uint8_t *)(SAVE_PET_ENTRY+18u),entry_n,*(uint8_t *)(SAVE_PET_ENTRY+16u));EG_SRAM_CLOSE();
    if(!instance)return 0u;pet_serial=instance;return pocket_add_pet(id,p.hp,0u,0u,instance);
  }
  if(!id || pocket_n>=POCKET_CAP || !eg_bag_put(pocket,pocket_hi,pocket_n,id))return 0u;
  memset(pet_meta+pocket_n*3u,0,3u);
  pocket_n++;if(!tool_piece)tool_piece=id;collection_mark(id);hud_dirty=puzzle_dirty=1u;effect_n=128u;return 1u;
}
uint8_t pocket_add_pet(eg_item_id id,uint8_t hp,uint8_t spent,uint8_t cooldown,uint8_t instance) BANKED {
  eg_pet_profile p;
  if(pocket_n>=POCKET_CAP || !instance || !eg_pet_get(id,&p) || !hp || hp>p.hp || !eg_bag_put(pocket,pocket_hi,pocket_n,id))return 0u;
  eg_pet_bag_put(pet_meta,pocket_n,hp,spent,cooldown,instance);pocket_n++;collection_mark(id);hud_dirty=puzzle_dirty=1u;effect_n=128u;return 1u;
}
void pocket_take(uint8_t slot) BANKED {uint8_t n=pocket_n;if(eg_bag_remove(pocket,pocket_hi,&pocket_n,slot)){eg_pet_bag_remove(pet_meta,n,slot);hud_dirty=puzzle_dirty=1u;effect_n=128u;}}
void pocket_entry_snapshot(uint8_t *lo,uint8_t *hi,uint8_t *n,eg_item_id *tool) BANKED {
 if(lo)memcpy(lo,entry_pocket,24u);if(hi)memcpy(hi,entry_hi,3u);*n=entry_n;*tool=(eg_item_id)(entry_tool|((uint16_t)entry_tool_hi<<8));
}
/* Change only the matching checkpoint individual. Picked ingredients and world
 * mutations still roll back together; a dead critter cannot reappear in that bag. */
uint8_t pocket_pet_event(uint8_t instance,const eg_pet_state *s,uint8_t dead) BANKED {
 uint8_t *entry=(uint8_t *)SAVE_PET_ENTRY,*meta=entry+18u,k,n=entry_n,hp=s->hp,metadata=0u;eg_pet_profile p;
 if(!instance)return 0u;EG_SRAM_OPEN();
 if(entry[16]==instance){
  if(dead){memset(entry,0,17u);if((eg_item_id)(entry_tool|((uint16_t)entry_tool_hi<<8))==s->id)entry_tool=entry_tool_hi=0u;}
  else {
   /* Pose, camera-relative position and animation buffers are presentation.
    * Persist the individual's life, charges and cooldowns without turning
    * ordinary movement into thirteen changed journal fields. */
   entry[4]=s->cd;entry[6]=(entry[6]&127u)|(s->state&PET_REVIVE_SPENT);
   entry[8]=s->hp;entry[9]=s->guard;entry[10]=s->buff&(uint8_t)~PB_RETALIATE;entry[11]=s->buff_t;entry[14]=s->auto_cd;
   if(s->buff&PB_RISING){entry[5]=24u;entry[6]=(entry[6]&248u)|PP_DEATH;entry[7]=255u;entry[12]=entry[13]=entry[15]=0u;}
  }
 } else {
  metadata=2u;
  for(k=0u;k<n;k++)if(meta[k*3u+2u]==instance)break;
  if(k==n){EG_SRAM_CLOSE();return 0u;}
  if(dead){
   eg_bag_remove(entry_pocket,entry_hi,&entry_n,k);eg_pet_bag_remove(meta,n,k);
   if((eg_item_id)(entry_tool|((uint16_t)entry_tool_hi<<8))==s->id && (eg_item_id)(entry[0]|((uint16_t)entry[1]<<8))!=s->id){
    for(k=0u;k<entry_n;k++)if(eg_bag_get(entry_pocket,entry_hi,k)==s->id)break;
    if(k==entry_n)entry_tool=entry_tool_hi=0u;
   }
  }
  else {
   if(!hp && (s->buff&PB_RISING) && eg_pet_get(s->id,&p))hp=(uint8_t)((p.hp+1u)>>1);
   eg_pet_bag_put(meta,k,hp,(uint8_t)(s->state&PET_REVIVE_SPENT),s->cd>s->auto_cd ? s->cd : s->auto_cd,instance);
  }
 }
 EG_SRAM_CLOSE();pet_save_dirty|=metadata ? 2u : 1u;return 1u;
}
uint8_t pocket_count(eg_item_id id) BANKED {uint8_t k,n=0u;for(k=0u;k<pocket_n;k++)if(pocket_id(k)==id)n++;return n;}
/* The conservative reset certificate has its own vocabulary. Crafted effects
 * are not decomposed into imaginary reagents. Its paused SRAM cache is rebuilt
 * whenever inventory, terrain or the active critter changes. */
uint8_t __at(0xDBBD) effect_n;
static uint8_t covered(const uint8_t *out,uint8_t n,uint8_t marker){
 uint8_t k,p;for(k=0u;k<n;k++){p=out[k];if(p==marker || (marker==EG_ASSIST_HOT && EG_PIECE_HOT(p)) || (marker==EG_ASSIST_COLD && (p==P_SNOW || p==P_ICE)) || (marker==EG_ASSIST_POWER && p==P_ELECTRIC) || (marker==EG_ASSIST_MUD && p==P_MUD) || (marker==EG_ASSIST_WEIGHT && p==P_BRICK))return 1u;}return 0u;
}
uint8_t pocket_effects(uint8_t *out) BANKED {
  uint8_t counts[24]={0},k,n=0u,f,caps,j;eg_item_id id;eg_item_profile profile;
  for(k=0u;k<pocket_n;k++) {
    id=pocket_id(k);
    if(id>=EG_BOMBS_FIRST) {
      eg_catalogue_profile(id,&profile);f=profile.effect;
      f=EG_PIECE_HOT(f) ? EG_ASSIST_HOT : (f==P_SNOW || f==P_ICE) ? EG_ASSIST_COLD : f==P_ELECTRIC ? EG_ASSIST_POWER : f==P_MUD ? EG_ASSIST_MUD : f==P_BRICK ? EG_ASSIST_WEIGHT : 0u;
      if(profile.kind==CK_TOOL && f==EG_ASSIST_WEIGHT){
        f=0u;for(j=0u;j<n;j++)if(out[j]==EG_ASSIST_WEIGHT || out[j]==P_BRICK)f++;
        while(f<2u){out[n++]=EG_ASSIST_WEIGHT;f++;}
      }else if(f)out[n++]=f;
      continue;
    }
    f=id<=24u ? (uint8_t)id : eg_catalogue_family(id);
    if(!f || f>24u)continue;counts[f-1u]++;
    if(f<=16u)out[n++]=f;else if(f==21u)out[n++]=EG_ASSIST_WEIGHT;
  }
  caps=eg_catalogue_capabilities(counts);
  if((caps&CC_HOT) && !covered(out,n,EG_ASSIST_HOT))out[n++]=EG_ASSIST_HOT;
  if((caps&CC_COLD) && !covered(out,n,EG_ASSIST_COLD))out[n++]=EG_ASSIST_COLD;
  if((caps&CC_POWER) && !covered(out,n,EG_ASSIST_POWER))out[n++]=EG_ASSIST_POWER;
  if((caps&CC_MUD) && !covered(out,n,EG_ASSIST_MUD))out[n++]=EG_ASSIST_MUD;
  if((caps&CC_WEIGHT) && !covered(out,n,EG_ASSIST_WEIGHT))out[n++]=EG_ASSIST_WEIGHT;
  f=companion_assist();
  if(f){for(k=0u;k<n;k++)if(out[k]==f)break;if(k==n && n<POCKET_EFFECT_CAP)out[n++]=f;}
  return n;
}
