#pragma bank 255
#include <string.h>
#include "play.h"
#include "heir.h"
#include "tutorial.h"
#include "voice.h"
#include "mapper.h"
#include "save_map.h"
#include "core/eg_journal.h"
uint8_t tutorial_on;
uint16_t tutorial_seen;
static uint8_t serial,slot,pending,cool;
static uint16_t queued_at;
uint8_t hud_narr_ready(void) BANKED;
void hud_narrate(const char *s) BANKED;
static const char *const cues[TE_COUNT]={"HOLD TO PUSH","WEIGHT HOLDS","A HOLD: CHARGE","B HOLD: CHARGE","SELECT: BENCH","DIR TWICE: ROLL","REST. RECOVER.","WEIGHT: PORTAL"};
#define TSLOT(k) ((uint8_t *)((k) ? SAVE_PREF_B : SAVE_PREF_A))
void tutorial_init(void) BANKED {
  uint8_t *p,*a=TSLOT(0u),*b=TSLOT(1u),va,vb;
  tutorial_on=voice_on=1u;tutorial_seen=0u;serial=slot=cool=0u;pending=255u;
  EG_SRAM_OPEN();va=eg_save_valid(a,SAVE_PREF_BYTES,1u);vb=eg_save_valid(b,SAVE_PREF_BYTES,1u);
  if(va || vb) {
    slot=(uint8_t)(vb && (!va || !eg_save_newer(a[1],b[1])));p=TSLOT(slot);serial=p[1];
    tutorial_on=(uint8_t)(p[2]!=0u);opt_move4=(uint8_t)(p[3]!=0u);music_volume=p[4]<=7u ? p[4] : 7u;
    sfx_volume=p[5]<=8u ? p[5] : 8u;voice_on=(uint8_t)(p[6]!=0u);tutorial_seen=(uint16_t)(p[7]|((uint16_t)p[8]<<8));
  }
  EG_SRAM_CLOSE();
}
void tutorial_save(void) BANKED {
  uint8_t rec[SAVE_PREF_BYTES],*p;
  memset(rec,0,sizeof rec);rec[0]=1u;rec[1]=++serial;rec[2]=tutorial_on;rec[3]=opt_move4;
  rec[4]=music_volume;rec[5]=sfx_volume;rec[6]=voice_on;rec[7]=(uint8_t)tutorial_seen;rec[8]=(uint8_t)(tutorial_seen>>8);
  eg_save_seal(rec,sizeof rec);slot^=1u;p=TSLOT(slot);
  EG_SRAM_OPEN();p[0]=0u;memcpy(p+1u,rec+1u,sizeof rec-1u);p[0]=1u;EG_SRAM_CLOSE();
}
void tutorial_queue(uint8_t event) BANKED {
  if(!tutorial_on || event>=TE_COUNT || (tutorial_seen&((uint16_t)1u<<event))) return;
  if(pending==255u) {pending=event;queued_at=frame_count;}
}
void tutorial_tick(void) BANKED {
  uint8_t k;
  if(!(frame_count&63u) && cool) cool--;
  if(pending==255u) return;
  if(!tutorial_on || (uint16_t)(frame_count-queued_at)>600u) {pending=255u;return;}
  if(cool || boss_floor || freeze || pl_state==PS_FALL || !hud_narr_ready()) return;
  for(k=0u;k<nen;k++) if(en[k].st!=ES_DEAD && absdiff((uint8_t)(en[k].x>>4),pl_x)<56u && absdiff((uint8_t)(en[k].y>>4),pl_y)<56u) return;
  hud_narrate(cues[pending]);tutorial_seen|=(uint16_t)1u<<pending;pending=255u;cool=10u;tutorial_save();
}
