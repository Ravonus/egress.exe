#ifndef STAMINA_H
#define STAMINA_H
#include "core/eg_stamina.h"
extern eg_stamina __at(0xDBA0) stamina;
void stamina_begin(void) BANKED;
uint8_t stamina_move(uint8_t move,uint8_t evade,uint8_t level) BANKED;
void stamina_tick(uint8_t busy) BANKED;
#endif
