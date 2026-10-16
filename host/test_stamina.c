#include <assert.h>
#include <stdio.h>
#include "../src/core/eg_stamina.h"
int main(void) {
  eg_stamina s;unsigned i,w,move;
  for(w=0;w<5;w++) for(move=0;move<EG_MV_COUNT;move++) {
    eg_stamina_begin(&s,4);assert(s.value==116);assert(eg_stamina_spend(&s,eg_stamina_cost(move,0,3,w)));
    for(i=0;i<30;i++) eg_stamina_tick(&s,1);assert(s.wait==24);
    for(i=0;i<260;i++) eg_stamina_tick(&s,0);assert(s.value==s.cap);
  }
  eg_stamina_begin(&s,0);assert(eg_stamina_spend(&s,100));assert(!eg_stamina_spend(&s,1));assert(!s.value);
  for(i=0;i<24;i++) eg_stamina_tick(&s,0);assert(!s.value);
  for(i=0;i<4;i++) eg_stamina_tick(&s,0);assert(s.value==2);
  assert(eg_stamina_cost(EG_MV_DODGE,0,0,0)>eg_stamina_cost(EG_MV_DODGE,1,0,0));
  assert(eg_stamina_cost(EG_MV_HIT3,0,0,0)>eg_stamina_cost(EG_MV_HIT1,0,0,0));
  assert(eg_stamina_cost(EG_MV_BPOWER,0,3,0)>eg_stamina_cost(EG_MV_SHOT,0,0,0));
  puts("stamina: all moves/weapons bounded; distinct evade/attack/charge costs, exhaustion and delayed recovery pass");
}
