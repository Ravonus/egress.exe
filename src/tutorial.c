#pragma bank 255
#include <string.h>
#include "play.h"
#include "heir.h"
#include "tutorial.h"
#include "voice.h"
#include "puzzle.h"
#include "stage.h"
#include "core/eg_input.h"
#include "core/eg_piece.h"
extern eg_input_t pin;
#include "mapper.h"
#include "save_map.h"
#include "core/eg_journal.h"
uint8_t tutorial_on;
uint16_t tutorial_seen;
static uint8_t serial,slot,pending,cool;
static uint16_t queued_at;
uint8_t hud_narr_ready(void) BANKED;
void hud_narrate(const char *s) BANKED;
static const char *const cues[10]={"WALK UP TO ITEMS","TAP A: STRIKE","HOLD A THEN RELEASE","TAP B: ONE SHOT","HOLD B THEN RELEASE","DIR TWICE: EVADE","PUSH BOX ONTO PLATE","START+A BENCH: MIX","FACE GAP. DIR TWICE","TAKE KEY. USE DOOR"};
#define TSLOT(k) ((uint8_t *)((k) ? SAVE_PREF_B : SAVE_PREF_A))
void tutorial_init(void) BANKED {
  uint8_t *p,*a=TSLOT(0u),*b=TSLOT(1u),va,vb;
  tutorial_on=0u;voice_on=1u;tutorial_seen=0u;serial=slot=cool=0u;pending=255u;
  EG_SRAM_OPEN();va=eg_save_valid(a,SAVE_PREF_BYTES,SAVE_PREF_VERSION);vb=eg_save_valid(b,SAVE_PREF_BYTES,SAVE_PREF_VERSION);
  if(va || vb) {
    slot=(uint8_t)(vb && (!va || !eg_save_newer(a[1],b[1])));p=TSLOT(slot);serial=p[1];
    opt_move4=(uint8_t)(p[3]!=0u);music_volume=p[4]<=7u ? p[4] : 7u;
    sfx_volume=p[5]<=8u ? p[5] : 8u;voice_on=(uint8_t)(p[6]!=0u);tutorial_seen=(uint16_t)(p[7]|((uint16_t)p[8]<<8));
  }
  EG_SRAM_CLOSE();
}
void tutorial_save(void) BANKED {
  uint8_t rec[SAVE_PREF_BYTES],*p;
  memset(rec,0,sizeof rec);rec[0]=SAVE_PREF_VERSION;rec[1]=++serial;rec[2]=0u;rec[3]=opt_move4;
  rec[4]=music_volume;rec[5]=sfx_volume;rec[6]=voice_on;rec[7]=(uint8_t)tutorial_seen;rec[8]=(uint8_t)(tutorial_seen>>8);
  eg_save_seal(rec,sizeof rec);slot^=1u;p=TSLOT(slot);
  EG_SRAM_OPEN();p[0]=0u;memcpy(p+1u,rec+1u,sizeof rec-1u);p[0]=SAVE_PREF_VERSION;EG_SRAM_CLOSE();
}
/* The course owns a fixed board and the existing hint scratch. Outside it
 * there is no tutorial switch and no first-use popup journal traffic. */
void tutorial_build(void) BANKED {
  uint8_t x,y,i;
  memset(&room,0,sizeof room);
  for(y=0u;y<EG_H;y++)for(x=0u;x<EG_W;x++) {
    i=EG_IDX(x,y);room.c[i]=(!x || x==15u || !y || y==13u || x==8u) ? T_WALL : T_EMPTY;
  }
  room.start=EG_IDX(3u,10u);room.door=EG_IDX(13u,2u);room.c[room.door]=T_DOOR;
  room.keys=1u;room.c[EG_IDX(12u,2u)]=T_KEY;
  room.c[EG_IDX(8u,6u)]=T_GATE;room.c[EG_IDX(6u,6u)]=T_PLATE;room.c[EG_IDX(5u,6u)]=T_BLOCK;
  room.c[EG_IDX(3u,7u)]=room.c[EG_IDX(4u,7u)]=T_ITEM;
  room.nitem=2u;room.item_cell[0]=EG_IDX(3u,7u);room.item_cell[1]=EG_IDX(4u,7u);
  room.item_piece[0]=P_FIRE;room.item_piece[1]=P_WATER;
  for(x=9u;x<15u;x++)room.c[EG_IDX(x,4u)]=T_PIT;
  room.c[EG_IDX(11u,7u)]=T_GLASS;room.c[EG_IDX(12u,7u)]=T_CEIL;
  pending=0u;cool=0u;queued_at=0u;
}
uint8_t tutorial_ready(void) BANKED {return (uint8_t)(pending>=9u);}
void tutorial_queue(uint8_t event) BANKED {
  if(tutorial_on==2u && event<TE_COUNT)queued_at|=(uint16_t)1u<<event;
}
void tutorial_tick(void) BANKED {
  uint8_t done=0u,k;
  if(tutorial_on!=2u)return;
  switch(pending) {
    case 0u:done=(uint8_t)(pl_y<136u);break;
    case 1u:done=(uint8_t)((queued_at&(1u<<TE_A))!=0u);break;
    case 2u:done=(uint8_t)(pin.move==EG_MV_CHARGED);break;
    case 3u:done=(uint8_t)(pin.move==EG_MV_SHOT);break;
    case 4u:done=(uint8_t)(pin.move==EG_MV_BPOWER);break;
    case 5u:done=(uint8_t)((queued_at&(1u<<TE_EVADE))!=0u);break;
    case 6u:done=(uint8_t)(room.c[EG_IDX(6u,6u)]==T_BLOCK_PLATE);break;
    case 7u:for(k=0u;k<pocket_n;k++)if(pocket[k]==P_STEAM)done=1u;break;
    case 8u:done=(uint8_t)(pl_x>=152u && pl_y<64u && !pl_air);break;
  }
  if(done) {pending++;cool=0u;sfx(SFX_KEY);}
  if(cool)cool--;
  else if(pl_state!=PS_FALL && !freeze) {hud_msg((queued_at&(1u<<TE_STAMINA)) && (pending==2u || pending==4u) ? (pending==2u ? "REST THEN HOLD A" : "REST THEN HOLD B") : cues[pending]);cool=180u;}
}
