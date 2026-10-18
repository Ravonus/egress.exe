#include <assert.h>
#include <stdio.h>
#include "eg_health.h"
int main(void) {
 eg_pet_state pet;eg_pet_profile p;uint8_t hp,q,i,before;
 eg_pet_start(&pet,433u);assert(eg_pet_get(pet.id,&p));
 for(i=1u;i<=24u;i++){
  hp=(uint8_t)((i+1u)>>1);q=i&1u;pet.hp=2u;
  if(i<24u){assert(eg_health_pickup(&hp,12u,&q,&pet,4u)==1u);assert(hp*2u-q==(i+4u>24u ? 24u : i+4u));assert(pet.hp==2u);}
  else {assert(eg_health_pickup(&hp,12u,&q,&pet,4u)==2u);assert(pet.hp==4u);}
 }
 hp=12u;q=0u;pet.hp=p.hp;assert(!eg_health_pickup(&hp,12u,&q,&pet,4u));
 pet.hp=0u;assert(!eg_health_pickup(&hp,12u,&q,&pet,4u)); /* health is never resurrection */
 hp=0u;pet.hp=2u;assert(!eg_health_pickup(&hp,12u,&q,&pet,4u));
 hp=12u;q=0u;pet.hp=p.hp-1u;assert(eg_health_pickup(&hp,12u,&q,&pet,8u)==2u && pet.hp==p.hp);
 before=pet.hp;assert(!eg_health_pickup(&hp,12u,&q,&pet,0u) && pet.hp==before);
 puts("Health pickups prioritise the heir, preserve quarter hearts, heal living pets only at full heir health, clamp to maximum and never revive.");return 0;
}
