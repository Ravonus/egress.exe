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
uint8_t eg_lift_strength(uint8_t weapon,uint8_t power,uint16_t generation) EG_BANKED {
  if((weapon!=1u && weapon!=3u) || power<2u || generation<6u) return 1u;
  return (uint8_t)(weapon==1u && power>=3u && generation>=18u ? 3u : 2u);
}
uint8_t eg_lift_allowed(uint8_t load,uint8_t height,uint8_t strength) EG_BANKED {
  if(!load || !height || !strength || strength>3u || load>strength || height>strength) return 0u;
  return (uint8_t)(load*height<=strength);
}
uint8_t eg_lift_cost(uint8_t load,uint8_t weapon,uint8_t power,uint8_t holding) EG_BANKED {
  uint8_t unit=weapon==1u ? 2u : weapon==3u ? 3u : 4u;
  uint16_t cost;
  if(!load || load>28u) return 0u;
  if(power>=3u)unit--;
  cost=(uint16_t)load*(holding ? unit : unit*3u);
  return (uint8_t)(cost>255u ? 255u : cost);
}
#if defined(__SDCC) && defined(__PORT_sm83)
static uint8_t meter_div5(uint16_t amount,uint16_t denom) __naked {
  amount;denom;
  __asm
    ld h,#16
    ld l,#0
  1$:
    ld a,d
    cp a,b
    jr c,3$
    jr nz,2$
    ld a,e
    cp a,c
    jr c,3$
  2$:
    ld a,e
    sub a,c
    ld e,a
    ld a,d
    sbc a,b
    ld d,a
    ld a,l
    or a,h
    ld l,a
  3$:
    srl b
    rr c
    srl h
    jr nz,1$
    ld a,l
    ret
  __endasm;
}
#endif
uint8_t eg_stamina_meter(uint8_t value,uint8_t cap,uint8_t units) EG_BANKED {
  uint16_t amount,denom;
  if(units>24u) units=24u;
  if(!cap || value>=cap) return units;
  if(!value) return 0u;
  amount=(uint16_t)value*units;denom=(uint16_t)cap<<4;
#if defined(__SDCC) && defined(__PORT_sm83)
  return meter_div5(amount,denom);
#else
  {
  uint8_t bit=16u,result=0u;
  /* The quotient fits five bits; the library divide always processes sixteen. */
  do {
    if(amount>=denom) {amount-=denom;result|=bit;}
    denom>>=1;bit>>=1;
  } while(bit);
  return result;
  }
#endif
}
