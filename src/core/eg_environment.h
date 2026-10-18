#ifndef EG_ENVIRONMENT_H
#define EG_ENVIRONMENT_H
#include "eg_room.h"
/* Persistent states use the existing material + skin packet, never gravity's
 * transient high bits. 255 means the action leaves the state unchanged. */
enum { EN_SOIL,EN_SEED,EN_SPROUT,EN_TREE,EN_ASH,EN_WATER,EN_ICE,EN_FOLIAGE,EN_STATES };
enum { EA_PLANT,EA_WATER,EA_FIRE,EA_COLD,EA_ACTIONS };
#define EN_NO_CHANGE 255u
uint8_t eg_environment_next(uint8_t state,uint8_t action) EG_BANKED;
#endif
