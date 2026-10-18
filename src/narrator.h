#ifndef NARRATOR_H
#define NARRATOR_H
#include <gb/gb.h>
#include "core/eg_narrator.h"
extern uint16_t narrator_cue;
extern uint8_t narrator_seq;
void narrator_begin(void) BANKED;
void narrator_event(uint8_t event,uint8_t detail) BANKED;
void narrator_tick(void) BANKED;
#define NARRATOR_SAVE_BYTES 49u
void narrator_store(uint8_t *out49) BANKED;
void narrator_restore(const uint8_t *in45) BANKED;
#endif
