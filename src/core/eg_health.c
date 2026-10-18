#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_health.h"
uint8_t eg_health_pickup(uint8_t *hp,uint8_t maximum,uint8_t *quarter,eg_pet_state *pet,uint8_t amount) EG_BANKED {
 uint8_t have,cap,n;eg_pet_profile p;
 if(!amount || !*hp || !maximum)return 0u;
 if(amount>8u)amount=8u;cap=(uint8_t)(maximum<<1);have=(uint8_t)((*hp<<1)-(*quarter&1u));
 if(have<cap){
  n=(uint8_t)(cap-have);if(n>amount)n=amount;have+=n;
  *hp=(uint8_t)((have+1u)>>1);*quarter=have&1u;return 1u;
 }
 if(!pet || !pet->hp || !eg_pet_get(pet->id,&p) || pet->hp>=p.hp)return 0u;
 n=(uint8_t)((amount+1u)>>1);pet->hp=(uint8_t)(p.hp-pet->hp<n ? p.hp : pet->hp+n);return 2u;
}
