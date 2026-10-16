#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_stamina.h"
void eg_stamina_begin(eg_stamina *s,uint8_t vigor) EG_BANKED {
  s->cap=(uint8_t)(100u+(vigor>4u ? 4u : vigor)*4u);s->value=s->cap;s->wait=s->pulse=0u;
}
uint8_t eg_stamina_cost(uint8_t move,uint8_t evade,uint8_t level,uint8_t weapon) EG_BANKED {
  static const uint8_t costs[EG_MV_COUNT]={0,24,1,0,18,8,10,14,0,20,12,10,0,18};
  uint8_t c;if(move>=EG_MV_COUNT) return 0u;c=costs[move];
  if(move==EG_MV_DODGE) return evade==EG_EVADE_SIDE ? 12u : evade==EG_EVADE_BACK ? 16u : 24u;
  if(move==EG_MV_CHARGED || move==EG_MV_BPOWER) c=(uint8_t)(c+(level>3u ? 3u : level)*6u);
  if(c && move>=EG_MV_LUNGE && move!=EG_MV_PARRY) {
    if(weapon==1u) c+=3u;else if(weapon==3u) c-=2u;else if(weapon==4u) c+=2u;
  }
  return c;
}
uint8_t eg_stamina_spend(eg_stamina *s,uint8_t cost) EG_BANKED {
  if(cost>s->value) return 0u;
  if(cost) {s->value-=cost;s->wait=24u;s->pulse=0u;}
  return 1u;
}
void eg_stamina_tick(eg_stamina *s,uint8_t busy) EG_BANKED {
  if(busy) {s->wait=24u;s->pulse=0u;return;}
  if(s->wait) {s->wait--;return;}
  if(s->value<s->cap && ++s->pulse==4u) {s->pulse=0u;s->value=(uint8_t)(s->cap-s->value<2u ? s->cap : s->value+2u);}
}
