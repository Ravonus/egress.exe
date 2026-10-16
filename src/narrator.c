/* Event-driven commentary. No dialogue lock and no extra pixels over the play field. */
#pragma bank 255
#include <string.h>
#include "play.h"
#include "heir.h"
#include "narrator.h"
#include "voice.h"
static eg_narrator __at(0xDB60) story;
typedef char narrator_state_fits[(sizeof(eg_narrator)==48u) ? 1 : -1];
static char caption[15];
uint16_t narrator_cue;
uint8_t narrator_seq;
uint8_t hud_narr_ready(void) BANKED;
void hud_narrate(const char *s) BANKED;
void narrator_begin(void) BANKED {
  eg_narr_begin(&story,run_seed);narrator_cue=NARR_NONE;narrator_seq=0u;
}
void narrator_event(uint8_t event,uint8_t detail) BANKED {if(!dbg_heir) eg_narr_event(&story,event,detail);}
void narrator_store(uint8_t *p) BANKED {p[0]=2u;memcpy(p+1u,&story,sizeof story);}
void narrator_restore(const uint8_t *p) BANKED {
  if(p[0]!=2u) return;
  memcpy(&story,p+1u,sizeof story);
  if((story.pending>=NE_COUNT && story.pending!=255u) || story.cursor>=4u || !story.rng) eg_narr_begin(&story,run_seed);
}
static uint8_t spoken_clip(uint8_t event,uint8_t detail) {
  switch(event) {
    case NE_HURT:case NE_CLOSE:return 0u;
    case NE_FALL:return 1u;case NE_JUMP:return 2u;case NE_BREAK:return 3u;
    case NE_CRUSH:return 4u;case NE_USE:return detail&128u ? 5u : 255u;
    case NE_RETURN:return 6u;case NE_ANCESTOR:return 7u;default:return 255u;
  }
}
void narrator_tick(void) BANKED {
  uint8_t k,clip;uint16_t cue;
  if(dbg_heir || (frame_count&63u)) return;
  story.now++;
  if(story.pending==255u) return;
  clip=voice_on ? spoken_clip(story.pending,story.detail) : 255u;
  if(clip!=255u) {
    cue=eg_narr_take(&story,voice_busy,1u);
    if(cue!=NARR_NONE && voice_start(clip)) {narrator_cue=cue;narrator_seq++;}
    return;
  }
  /* Voice can be less sparse when its assets are enabled. Text waits for a safe, quiet moment. */
  if(boss_floor || board_busy || freeze || !pl_hp || !hud_narr_ready()) {eg_narr_take(&story,1u,0u);return;}
  for(k=0u;k<nen;k++) if(en[k].st!=ES_DEAD && absdiff((uint8_t)(en[k].x>>4),pl_x)<56u && absdiff((uint8_t)(en[k].y>>4),pl_y)<56u) {
    eg_narr_take(&story,1u,0u);return;
  }
  cue=eg_narr_take(&story,0u,0u);
  if(cue==NARR_NONE) return;
  narrator_cue=cue;narrator_seq++;eg_narr_caption(cue,caption);hud_narrate(caption);
}
