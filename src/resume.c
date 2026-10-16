/* Native continue journal. The frozen floor-entry payload is retained when only HP changes. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "stage.h"
#include "family.h"
#include "heir.h"
#include "mob.h"
#include "run.h"
#include "resume.h"
#include "save_map.h"
#include "mapper.h"
#include "core/eg_journal.h"
#include "narrator.h"
#include "stamina.h"
#include "boss_api.h"
static uint8_t __at(0xDBF0) snap[SAVE_CONT_BYTES];
uint8_t resume_ready,resume_state,resume_rest,resume_new;
static uint8_t serial,slot;
#define RSLOT(k) ((uint8_t *)((k) ? SAVE_CONT_B : SAVE_CONT_A))
typedef char resume_plan_fits[(sizeof(eg_plan_state)<=40u) ? 1 : -1];
typedef char resume_slice_fits[(sizeof(eg_slice)<=23u) ? 1 : -1];
typedef char resume_acts_fits[(sizeof(floor_acts_t)<=12u) ? 1 : -1];
static uint16_t word(uint8_t at) {return (uint16_t)(snap[at]|((uint16_t)snap[at+1u]<<8));}
static void put(uint8_t at,uint16_t v) {snap[at]=(uint8_t)v;snap[at+1u]=(uint8_t)(v>>8);}
static uint8_t valid(uint8_t *p) {
  if(!eg_save_valid(p,SAVE_CONT_BYTES,1u) || p[2]>RES_WON) return 0u;
  if(!p[2]) return 1u;
  return (uint8_t)(p[23] && p[23]<=63u && p[65]>=6u && p[65]<=20u && p[64]<=p[65] && p[70]<=POCKET_CAP);
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
static void write(void) {
  uint8_t *p;
  snap[0]=1u;snap[1]=++serial;eg_save_seal(snap,SAVE_CONT_BYTES);slot^=1u;p=RSLOT(slot);
  EG_SRAM_OPEN();p[0]=0u;memcpy(p+1u,snap+1u,SAVE_CONT_BYTES-1u);p[0]=1u;EG_SRAM_CLOSE();
  resume_state=snap[2];resume_ready=(uint8_t)(resume_state==RES_FLOOR || resume_state==RES_LANDING);
}
void resume_save(uint8_t state) BANKED {
  if(dbg_heir) return;
  memset(snap,0,sizeof snap);snap[2]=state;
  put(4u,run_seed);put(10u,family.generation);snap[12]=family.selected;put(13u,family.current.seed);
  snap[15]=resume_rest;put(16u,plan.seen);snap[23]=floor_no;put(24u,floor_seed);snap[26]=floor_tier;memcpy(snap+28u,&stamina,sizeof stamina);snap[34]=heart_shards;
  snap[64]=pl_hp;snap[65]=pl_hp_max;snap[66]=tokens;snap[67]=mods;snap[68]=kills_run;snap[69]=wmods;
  snap[70]=pocket_n;memcpy(snap+71u,pocket,POCKET_CAP);
  memcpy(snap+95u,truth,sizeof truth);memcpy(snap+104u,standing,sizeof standing);
  run_resume_get(snap+110u);put(112u,meta.runs);snap[114]=meta.wins;put(115u,eg_rs);snap[117]=floor_resets;
  narrator_store(snap+118u);snap[245]=run_save_serial();
  memcpy(snap+168u,&plan,sizeof plan);memcpy(snap+208u,&slice,sizeof slice);memcpy(snap+232u,&acts,sizeof acts);
  write();
}
static void read(void) {
  EG_SRAM_OPEN();memcpy(snap,RSLOT(slot),sizeof snap);EG_SRAM_CLOSE();
}
static void apply(void) {
  run_seed=word(4u);resume_rest=snap[15];floor_no=snap[23];floor_seed=word(24u);floor_tier=snap[26];memcpy(&stamina,snap+28u,sizeof stamina);
  heart_shards=snap[34]<4u ? snap[34] : 0u;
  if(stamina.cap<100u || stamina.cap>116u || stamina.value>stamina.cap) stamina_begin();
  pl_hp=snap[64];pl_hp_max=snap[65];tokens=snap[66];mods=snap[67];kills_run=snap[68];wmods=snap[69];
  pocket_n=snap[70];memcpy(pocket,snap+71u,POCKET_CAP);memcpy(truth,snap+95u,sizeof truth);
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
void resume_damage(void) BANKED {
  if(dbg_heir || !resume_ready || resume_state!=RES_FLOOR) return;
  snap[64]=pl_hp;memcpy(snap+28u,&stamina,sizeof stamina);narrator_store(snap+118u);write();
}
void resume_clear(void) BANKED {
  if(dbg_heir) return;
  memset(snap,0,sizeof snap);write();write();resume_rest=0u;
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
