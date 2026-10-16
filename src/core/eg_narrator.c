#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_narrator.h"
static const uint8_t priority[NE_COUNT]={1,2,1,2,2,2,4,3,6,7,3,5,2,4,5,4,2,3,2,4,5,1,6,4};
static const char *const captions[NE_COUNT]={"BY HAND. YES.","GRAVITY, AGAIN","WALL DISAGREES","THIRD THING.",
  "USED. PROBABLY","STILL YOURS.","ROOM. AGAIN.","COMPANY. OH.","ALMOST, THERE.","THAT WAS YOU.",
  "RELATED, I SEE","FAMILIAR FLOOR","BACK HERE? YES","THE SHORT WAY.","APPARENTLY OUT","ROOM SAYS NO.",
  "ONE LESS FOE.","RETURN TO...", "AN OFFICE NAP.","GRAVITY WINS.","THAT CONNECTED","A POINT MADE.","LESS FLOOR.","ACROSS. YES."};
static uint8_t next(eg_narrator *s) {
  s->rng^=s->rng<<7;s->rng^=s->rng>>9;s->rng^=s->rng<<8;return (uint8_t)s->rng;
}
static uint8_t add(uint8_t a,uint8_t b) {return a>255u-b ? 255u : (uint8_t)(a+b);}
void eg_narr_begin(eg_narrator *s,uint16_t seed) EG_BANKED {
  uint8_t k;memset(s,0,sizeof *s);s->rng=seed ? seed : 1u;s->last=(uint16_t)(0u-NARR_TEXT_GAP);
  s->pending=s->last_topic=255u;for(k=0;k!=4u;k++) s->recent[k]=NARR_NONE;
}
void eg_narr_event(eg_narrator *s,uint8_t event,uint8_t detail) EG_BANKED {
  if(event>=NE_COUNT) return;
  s->count[event]=add(s->count[event],1u);
  if(event==NE_REFUSED || event==NE_RESET || event==NE_CLOSE) s->confusion=add(s->confusion,3u);
  if(event==NE_MIX || event==NE_CLEAR) s->confidence=add(s->confidence,2u);
  if(event==NE_PUSH && s->count[event]<3u) return;
  if(event==s->last_topic) return;
  if(s->pending!=255u && priority[event]<s->priority) return;
  s->pending=event;s->priority=priority[event];s->detail=detail;s->queued_at=s->now;
}
uint16_t eg_narr_take(eg_narrator *s,uint8_t busy,uint8_t voice) EG_BANKED {
  uint8_t topic=s->pending,variant,intro,aside,k;
  uint16_t line;
  if(topic>=NE_COUNT) return NARR_NONE;
  if((uint16_t)(s->now-s->queued_at)>(voice ? 4u : 30u)) {s->pending=255u;return NARR_NONE;}
  if(busy || (!s->emitted && s->now<(voice ? 2u : 30u)) || (uint16_t)(s->now-s->last)<(voice ? NARR_VOICE_GAP : NARR_TEXT_GAP)) return NARR_NONE;
  variant=(uint8_t)(next(s)&3u);intro=(uint8_t)(next(s)&3u);aside=(uint8_t)(next(s)&7u);
  /* Last-item wording is allowed only when the successful-use event recorded that fact. */
  if(topic==NE_USE && !(s->detail&128u) && variant==1u) variant=0u;
  if(s->confusion>s->confidence) aside|=4u;
  line=(uint16_t)(topic|((uint16_t)variant<<5)|((uint16_t)intro<<7)|((uint16_t)aside<<9));
  for(;;) {
    for(k=0;k!=4u;k++) if(s->recent[k]==line) break;
    if(k==4u) break;
    aside=(uint8_t)((aside+1u)&7u);line=(uint16_t)((line&511u)|((uint16_t)aside<<9));
  }
  s->recent[s->cursor]=line;s->cursor=(uint8_t)((s->cursor+1u)&3u);s->last=s->now;
  s->last_topic=topic;s->pending=255u;s->emitted=add(s->emitted,1u);return line;
}
void eg_narr_caption(uint16_t line,char *out15) EG_BANKED {
  const char *p;uint8_t n=0u;if((line&31u)>=NE_COUNT) {out15[0]=0;return;}
  p=captions[line&31u];while(*p && n<14u) out15[n++]=*p++;out15[n]=0;
}
#ifndef __SDCC
static const char *const openings[4]={"Right. ","Apparently, ","Oh. ","Let us be clear. "};
static const char *const asides[8]={" I think."," That sounds right."," This was almost planned."," That is the official version.",
  " Unless that was before."," I have several incompatible notes."," Please disregard the other notes."," I shall supervise from elsewhere."};
static const char *const observations[NE_COUNT][4]={
  {"The load was moved by hand.","A box has been persuaded.","That object was somewhere else before.","The furniture has received instructions."},
  {"Something heavy has settled.","Gravity has submitted its report.","The CRT has changed departments.","Something was holding that up. Was."},
  {"The wall has lost the argument.","There is less wall now.","A partition has become debris.","The partition has reconsidered."},
  {"Two things became a third.","A recipe has occurred.","The ingredients are no longer separate.","Something useful has been assembled."},
  {"A tool has been used.","That was your last carried item.","The material has made its point.","A resource has become a consequence."},
  {"That did nothing. You kept the items.","The attempt was refused. Nothing spent.","The ingredients remain unconvinced.","The building declined that suggestion."},
  {"The room has been reset.","The room is new again. The room disagrees.","We have filed the same room again.","The previous attempt has been withdrawn."},
  {"The room has arranged some company.","Another wave has been scheduled.","Company. That is the polite word.","More employees have been requested."},
  {"That was dangerously close.","The heir is running short of life.","There is very little margin left.","The situation has become rather personal."},
  {"That heir has stopped.","The family has another ending.","The building has kept someone.","A life has concluded inconveniently."},
  {"An heir has been chosen.","The family has sent someone.","A selection has been made.","Someone has accepted the family business."},
  {"A parent died on this floor.","This floor appears in the family history.","The family has lost someone here.","There is an ancestor attached to this place."},
  {"You have returned here.","This place is being visited again.","We were here before.","The route has folded back on itself."},
  {"The shortcut has been taken.","Some floors have been left behind.","The short way has been chosen.","A door has shortened the descent."},
  {"The exit has been reached.","That floor is behind you now.","Another room has been left.","You have left that particular arrangement."},
  {"The room says it has forgotten.","The building is questioning this route.","The room has issued an objection.","The room is expressing some doubt."},
  {"An enemy has been defeated.","There is one fewer enemy.","That particular threat has ended.","Someone has been removed from the rota."},
  {"An attack has been returned.","You have sent that back.","The attack has changed its opinion.","A return has been processed."},
  {"The heir has dozed off.","An office nap has occurred.","Someone has briefly stopped supervising.","The family has taken a moment."},
  {"An enemy has been crushed.","Gravity has resolved a disagreement.","A falling load has ended that threat.","The ceiling has assisted the heir."},
  {"They hit you.","That blow connected.","The heir has taken damage.","That was a painful exchange."},
  {"You have swung at it.","An attack has been attempted.","The heir has made a point. With a weapon.","You have committed to that swing."},
  {"There was less floor there than we needed.","The heir has fallen off.","You have found a hole.","That step has become a fall."},
  {"You made it across.","The gap has been crossed.","You landed on the other side.","That roll cleared the missing floor."}
};
void eg_narr_text(uint16_t line,char *out160) {
  if((line&31u)>=NE_COUNT) {out160[0]=0;return;}
  strcpy(out160,openings[(line>>7)&3u]);strcat(out160,observations[line&31u][(line>>5)&3u]);strcat(out160,asides[(line>>9)&7u]);
}
#endif
