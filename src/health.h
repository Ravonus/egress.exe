#ifndef EG_HEALTH_NATIVE_H
#define EG_HEALTH_NATIVE_H
#include <gb/gb.h>
void health_load(void) BANKED;
void health_reset(void) BANKED;
void health_shift_view(int8_t dx,int8_t dy) BANKED;
uint8_t health_pick(uint8_t cell) BANKED;
uint8_t health_drop(uint8_t cell) BANKED;
void health_draw(void) BANKED;
void health_art_load(void) BANKED;
#define ST_HEALTH 112u
#endif
