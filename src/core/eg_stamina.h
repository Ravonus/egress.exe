#ifndef EG_STAMINA_H
#define EG_STAMINA_H
#include "eg_input.h"
typedef struct {uint8_t value,cap,wait,pulse;} eg_stamina;
void eg_stamina_begin(eg_stamina *s,uint8_t vigor) EG_BANKED;
uint8_t eg_stamina_cost(uint8_t move,uint8_t evade,uint8_t level,uint8_t weapon) EG_BANKED;
uint8_t eg_stamina_spend(eg_stamina *s,uint8_t cost) EG_BANKED;
void eg_stamina_tick(eg_stamina *s,uint8_t busy) EG_BANKED;
#endif
