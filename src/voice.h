#ifndef VOICE_H
#define VOICE_H
#include <gb/gb.h>
extern volatile uint8_t voice_busy,voice_dirty;
extern uint8_t voice_clip,voice_seq,voice_on;
void voice_init(void) NONBANKED;
void voice_stop(void) NONBANKED;
uint8_t voice_start(uint8_t clip) NONBANKED;
#endif
