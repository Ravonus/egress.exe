#ifndef EG_NARRATOR_H
#define EG_NARRATOR_H
#include "eg_room.h"
enum { NE_PUSH,NE_DROP,NE_BREAK,NE_MIX,NE_USE,NE_REFUSED,NE_RESET,NE_WAVE,
  NE_CLOSE,NE_DEATH,NE_HEIR,NE_ANCESTOR,NE_RETURN,NE_SHORTCUT,NE_CLEAR,NE_STUCK,
  NE_KILL,NE_PARRY,NE_DOZE,NE_CRUSH,NE_HURT,NE_ATTACK,NE_FALL,NE_JUMP,NE_COUNT };
#define NARR_NONE 65535u
#define NARR_TEXT_GAP 90u
#define NARR_VOICE_GAP 14u
typedef struct {
  uint16_t rng,now,last,queued_at,recent[4];
  uint8_t count[NE_COUNT];
  uint8_t pending,priority,detail,confusion,confidence,cursor,emitted,last_topic;
} eg_narrator;
void eg_narr_begin(eg_narrator *s,uint16_t seed) EG_BANKED;
void eg_narr_event(eg_narrator *s,uint8_t event,uint8_t detail) EG_BANKED;
/* busy suppresses emission without stopping gameplay. Text is deliberately less frequent than voice. */
uint16_t eg_narr_take(eg_narrator *s,uint8_t busy,uint8_t voice) EG_BANKED;
void eg_narr_caption(uint16_t line,char *out15) EG_BANKED;
#ifndef __SDCC
/* One canonical grammar feeds the offline speech baker and browser catalogue. */
void eg_narr_text(uint16_t line,char *out160);
#endif
#endif
