#ifndef EG_STAMINA_H
#define EG_STAMINA_H
#include "eg_input.h"
typedef struct {uint8_t value,cap,wait,pulse;} eg_stamina;
typedef struct {uint8_t cell,height,load,strength;} eg_lift;
void eg_stamina_begin(eg_stamina *s,uint8_t vigor) EG_BANKED;
uint8_t eg_stamina_cost(uint8_t move,uint8_t evade,uint8_t level,uint8_t weapon) EG_BANKED;
uint8_t eg_stamina_spend(eg_stamina *s,uint8_t cost) EG_BANKED;
void eg_stamina_tick(eg_stamina *s,uint8_t busy) EG_BANKED;
/* Lift budget is monitors multiplied by tiles raised. Heavy classes grow
 * beyond one only with inherited power and later generations. */
uint8_t eg_lift_strength(uint8_t weapon,uint8_t power,uint16_t generation) EG_BANKED;
uint8_t eg_lift_allowed(uint8_t load,uint8_t height,uint8_t strength) EG_BANKED;
uint8_t eg_lift_cost(uint8_t load,uint8_t weapon,uint8_t power,uint8_t holding) EG_BANKED;
/* Exact filled units, 0..24, without a general 16-bit divide on each HUD update. */
uint8_t eg_stamina_meter(uint8_t value,uint8_t cap,uint8_t units) EG_BANKED;
#endif
