/* Native continue journal. The frozen floor-entry payload is retained when only HP changes. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "core/eg_piece.h"
#include "core/eg_bag.h"
#include "stage.h"
#include "family.h"
#include "heir.h"
#include "mob.h"
#include "run.h"
#include "tutorial.h"
#include "resume.h"
#include "save_map.h"
#include "mapper.h"
#include "core/eg_journal.h"
#include "narrator.h"
#include "stamina.h"
#include "boss_api.h"
#include "companion.h"
static uint8_t __at(0xDBF0) snap[SAVE_CONT_BYTES];
uint8_t resume_ready,resume_state,resume_rest,resume_new;
uint8_t __at(0xDDDB) pet_save_dirty;
static uint8_t serial,slot;
/* Copy the record body in fixed eight-byte bursts, leaving its version byte
 * invalid until its whole matching CRC record has reached SRAM. */
void resume_record_copy(uint8_t *out,const uint8_t *in) __naked {
 out;in;
 __asm
 ld h, b
 ld l, c
 ld b, a
 inc hl
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
1$:
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 ld a, (hl+)
 ld (de), a
 inc de
 dec b
 jr NZ, 1$
 ret
 __endasm;
}
static void record_copy112(uint8_t *out,const uint8_t *in) __naked {
 out;in;
 __asm
 ld a, #13
 jp _resume_record_copy
 __endasm;
}
static void record_copy256(uint8_t *out,const uint8_t *in) __naked {
 out;in;
 __asm
 ld a, #31
 jp _resume_record_copy
 __endasm;
}
#define RSLOT(k) ((uint8_t *)((k) ? SAVE_CONT_B : SAVE_CONT_A))
#define PETSLOT(k) ((uint8_t *)((k) ? SAVE_PET_B : SAVE_PET_A))
typedef char pet_journal_layout[(SAVE_PET_ENTRY+EG_PET_ENTRY_BYTES<=SAVE_PET_A && SAVE_PET_A+EG_PET_RECORD_BYTES==SAVE_PET_B && SAVE_PET_B+EG_PET_RECORD_BYTES==SAVE_PET_SCRATCH && SAVE_PET_SCRATCH+EG_PET_RECORD_BYTES==SAVE_FAMILY_A) ? 1 : -1];
typedef char resume_plan_fits[(sizeof(eg_plan_state)<=40u) ? 1 : -1];
typedef char resume_slice_fits[(sizeof(eg_slice)<=23u) ? 1 : -1];
typedef char resume_acts_fits[(sizeof(floor_acts_t)<=12u) ? 1 : -1];
static uint16_t word(uint8_t at) {return (uint16_t)(snap[at]|((uint16_t)snap[at+1u]<<8));}
static void put(uint8_t at,uint16_t v) {snap[at]=(uint8_t)v;snap[at+1u]=(uint8_t)(v>>8);}
static uint8_t valid(uint8_t *p) {
  uint8_t k;
  if(!eg_save_valid(p,SAVE_CONT_BYTES,SAVE_CONT_VERSION) || p[2]>RES_WON) return 0u;
  if(!p[2]) return 1u;
  if(!(p[23] && p[23]<=63u && p[65]>=6u && p[65]<=20u && p[64]<=p[65] &&
    eg_bag_valid(p+71u,p+37u,p[70]) && (uint16_t)(p[35]|((uint16_t)p[40]<<8))<=EG_DISCOVERIES))return 0u;
  if(p[6])return p[6]==1u && eg_pet_record_valid(PETSLOT(p==RSLOT(1u)),p+71u,p+37u,p[70],(uint16_t)(p[4]|((uint16_t)p[5]<<8)),p[23],p[1]);
  for(k=0u;k<p[70];k++)if(eg_bag_get(p+71u,p+37u,k)>=EG_PETS_FIRST)return 0u;
  return 1u;
}
uint8_t resume_probe(void) BANKED {
  uint8_t *a=RSLOT(0u),*b=RSLOT(1u),va,vb;
  EG_SRAM_OPEN();va=valid(a);vb=valid(b);
  slot=(uint8_t)(vb && (!va || !eg_save_newer(a[1],b[1])));
  if(va || vb) {a=RSLOT(slot);serial=a[1];resume_state=a[2];}
  else {serial=0u;slot=0u;resume_state=RES_NONE;}
  EG_SRAM_CLOSE();
  resume_ready=(uint8_t)(resume_state==RES_FLOOR || resume_state==RES_LANDING);
  return resume_ready;
}
static uint8_t has_pets(void) {
 uint8_t k,active;
 EG_SRAM_OPEN();active=(uint8_t)(*(uint8_t *)SAVE_PET_ENTRY|*(uint8_t *)(SAVE_PET_ENTRY+1u));EG_SRAM_CLOSE();
 if(active)return 1u;
 for(k=0u;k<snap[70];k++)if(eg_bag_get(snap+71u,snap+37u,k)>=EG_PETS_FIRST)return 1u;
 return 0u;
}
static void write(uint8_t fast) {
  uint8_t *p,*old,*scratch=(uint8_t *)SAVE_PET_SCRATCH,*entry=(uint8_t *)SAVE_PET_ENTRY,k,previous=serial;
  serial++;
  if(fast){eg_save_patch256(snap,0u,SAVE_CONT_VERSION);eg_save_patch256(snap,1u,serial);}
  else {snap[0]=SAVE_CONT_VERSION;snap[1]=serial;eg_save_seal(snap,SAVE_CONT_BYTES);}
  old=PETSLOT(slot);slot^=1u;EG_SRAM_OPEN();
  if(snap[2] && snap[6]){
    p=PETSLOT(slot);
    if(fast && !(pet_save_dirty&128u) && old[0]==EG_PET_RECORD_VERSION && old[1]==previous && old[2]==snap[4] && old[3]==snap[5] && old[4]==snap[23]){
      record_copy112(scratch,old);scratch[0]=old[0];eg_save_patch112(scratch,1u,serial);
      /* Ordinary life/ability events touch the active individual, not the
       * seventy-two immutable carried metadata bytes after it. */
      for(k=0u;k<18u;k++)if(scratch[8u+k]!=entry[k])eg_save_patch112(scratch,8u+k,entry[k]);
    } else eg_pet_record_pack(scratch,entry,word(4u),snap[23],serial);
    p[0]=0u;record_copy112(p,scratch);p[0]=EG_PET_RECORD_VERSION;
  }
  p=RSLOT(slot);p[0]=0u;record_copy256(p,snap);p[0]=SAVE_CONT_VERSION;EG_SRAM_CLOSE();pet_save_dirty=0u;
  resume_state=snap[2];resume_ready=(uint8_t)(resume_state==RES_FLOOR || resume_state==RES_LANDING);
}
void resume_save(uint8_t state) BANKED {
  if(dbg_heir || tutorial_on==2u) return;
  pocket_enter();
  memset(snap,0,sizeof snap);snap[2]=state;
  put(4u,run_seed);put(10u,family.generation);snap[12]=family.selected;put(13u,family.current.seed);
  snap[15]=resume_rest;put(16u,plan.seen);snap[23]=floor_no;put(24u,floor_seed);snap[26]=floor_tier;memcpy(snap+28u,&stamina,sizeof stamina);snap[34]=heart_shards;
  snap[64]=pl_hp;snap[65]=pl_hp_max;snap[66]=tokens;snap[67]=mods;snap[68]=kills_run;snap[69]=wmods;
  snap[35]=(uint8_t)tool_piece;snap[36]=pl_quarter;snap[40]=(uint8_t)(tool_piece>>8);memcpy(snap+37u,pocket_hi,3u);
  snap[70]=pocket_n;memcpy(snap+71u,pocket,POCKET_CAP);
  snap[6]=has_pets();pet_save_dirty=0u;
  memcpy(snap+95u,truth,sizeof truth);memcpy(snap+104u,standing,sizeof standing);
  run_resume_get(snap+110u);put(112u,meta.runs);snap[114]=meta.wins;put(115u,eg_rs);snap[117]=floor_resets;
  narrator_store(snap+118u);snap[245]=run_save_serial();
  memcpy(snap+168u,&plan,sizeof plan);memcpy(snap+208u,&slice,sizeof slice);memcpy(snap+232u,&acts,sizeof acts);
  write(0u);
}
static void read(void) {
  EG_SRAM_OPEN();memcpy(snap,RSLOT(slot),sizeof snap);
  if(snap[6])memcpy((uint8_t *)SAVE_PET_ENTRY,PETSLOT(slot)+8u,EG_PET_ENTRY_BYTES);else memset((uint8_t *)SAVE_PET_ENTRY,0,EG_PET_ENTRY_BYTES);
  EG_SRAM_CLOSE();
}
static void apply(void) {
  run_seed=word(4u);resume_rest=snap[15];floor_no=snap[23];floor_seed=word(24u);floor_tier=snap[26];memcpy(&stamina,snap+28u,sizeof stamina);
  heart_shards=snap[34]<4u ? snap[34] : 0u;
  if(stamina.cap<100u || stamina.cap>116u || stamina.value>stamina.cap) stamina_begin();
  pl_hp=snap[64];pl_hp_max=snap[65];tokens=snap[66];mods=snap[67];kills_run=snap[68];wmods=snap[69];
  pl_quarter=snap[36]&1u;
  tool_piece=(eg_item_id)(snap[35]|((uint16_t)snap[40]<<8));tool_cd=0u;
  pocket_n=snap[70];memcpy(pocket,snap+71u,POCKET_CAP);memcpy(pocket_hi,snap+37u,3u);memcpy(truth,snap+95u,sizeof truth);
  EG_SRAM_OPEN();memcpy(pet_meta,(const uint8_t *)(SAVE_PET_ENTRY+18u),sizeof pet_meta);pet_serial=*(uint8_t *)(SAVE_PET_ENTRY+17u);EG_SRAM_CLOSE();companion_restore();pet_save_dirty=0u;
  memcpy(standing,snap+104u,sizeof standing);run_resume_set(snap+110u);eg_rs=word(115u);floor_resets=snap[117];
  memcpy(&plan,snap+168u,sizeof plan);memcpy(&slice,snap+208u,sizeof slice);memcpy(&acts,snap+232u,sizeof acts);
  stage_kind=slice.kind;stage_type=slice.type;
  narrator_restore(snap+118u);
}
uint8_t resume_restore(void) BANKED {
  if(dbg_heir || !resume_probe()) return 0u;
  read();
  if(!family.active || family.generation!=word(10u) || family.current.seed!=word(13u)) {resume_clear();return 0u;}
  apply();return 1u;
}
static void damage_fields(uint8_t with_history) {
  uint8_t k,j,changes=0u;uint8_t *history=(uint8_t *)(SAVE_FUSION_SCRATCH+SAVE_FUSION_BYTES),*dirty=(uint8_t *)(SAVE_FUSION_SCRATCH+128u);
  eg_save_patch256(snap,64u,pl_hp);eg_save_patch256(snap,36u,pl_quarter);
  for(k=0u;k<sizeof stamina;k++)eg_save_patch256(snap,28u+k,((uint8_t *)&stamina)[k]);
  if(with_history){
    EG_SRAM_OPEN();narrator_store(history);
    for(k=0u;k<NARRATOR_SAVE_BYTES;k++)if(snap[118u+k]!=history[k])dirty[changes++]=k;
    if(changes>=14u){
      memcpy(snap+118u,history,NARRATOR_SAVE_BYTES);eg_save_seal(snap,SAVE_CONT_BYTES);
    }else for(j=0u;j<changes;j++){k=dirty[j];eg_save_patch256(snap,118u+k,history[k]);}
    EG_SRAM_CLOSE();
  }
  write(1u);
}
void resume_damage(void) BANKED {
  if(dbg_heir || tutorial_on==2u || !resume_ready || resume_state!=RES_FLOOR) return;
  if(pet_save_dirty&127u){pet_save_dirty|=4u;resume_pet_flush();return;}
  damage_fields(1u);
}
void resume_pet_flush(void) BANKED {
 uint8_t k,metadata=pet_save_dirty&2u,history=pet_save_dirty&4u,*tmp=(uint8_t *)(SAVE_FUSION_SCRATCH+64u);
 if(!pet_save_dirty)return;
 if(dbg_heir || tutorial_on==2u || !resume_ready || resume_state!=RES_FLOOR){pet_save_dirty=0u;return;}
 EG_SRAM_OPEN();pocket_entry_snapshot(metadata ? tmp : 0,metadata ? tmp+24u : 0,tmp+27u,(eg_item_id *)(tmp+28u));
 if(metadata){
  for(k=0u;k<24u;k++)if(snap[71u+k]!=tmp[k])eg_save_patch256(snap,71u+k,tmp[k]);
  for(k=0u;k<3u;k++)if(snap[37u+k]!=tmp[24u+k])eg_save_patch256(snap,37u+k,tmp[24u+k]);
  eg_save_patch256(snap,70u,tmp[27u]);
 }
 eg_save_patch256(snap,35u,tmp[28u]);eg_save_patch256(snap,40u,tmp[29u]);EG_SRAM_CLOSE();
 /* Bit 7 is synchronous write context; lower bits queue live changes. */
 eg_save_patch256(snap,6u,has_pets());pet_save_dirty=metadata ? 128u : 0u;
 /* Pet-only durability does not checkpoint unrelated room/narrator progress.
  * A coalesced heir-damage request retains its complete history snapshot. */
 damage_fields(history);
}
void resume_clear(void) BANKED {
  if(dbg_heir || tutorial_on==2u) return;
  memset(snap,0,sizeof snap);write(0u);write(0u);resume_rest=0u;pet_save_dirty=0u;
}
uint8_t resume_finish_pending(void) BANKED {
  uint8_t won;
  resume_probe();
  if(resume_state!=RES_DEAD && resume_state!=RES_WON) return 0u;
  read();won=(uint8_t)(resume_state==RES_WON);apply();
  if(run_save_serial()==snap[245]) run_end(won);
  if(family.active && family.generation==word(10u) && family.current.seed==word(13u)) family_end(won);
  resume_clear();return 1u;
}
