#ifndef EG_HEALTH_H
#define EG_HEALTH_H
#include "eg_pet.h"
/* Amount is in quarter hearts. Never revives a dead heir or critter. A healthy
 * heir routes the complete pickup to the living companion; a full pair leaves it. */
uint8_t eg_health_pickup(uint8_t *hp,uint8_t maximum,uint8_t *quarter,eg_pet_state *pet,uint8_t amount) EG_BANKED;
#endif
