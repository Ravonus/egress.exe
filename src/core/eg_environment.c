#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_environment.h"
uint8_t eg_environment_next(uint8_t state,uint8_t action) EG_BANKED {
 if(action==EA_PLANT)return state==EN_SOIL || state==EN_ASH ? EN_SEED : EN_NO_CHANGE;
 if(action==EA_WATER)return state==EN_SEED ? EN_SPROUT : state==EN_SPROUT ? EN_TREE : EN_NO_CHANGE;
 if(action==EA_FIRE){
  if(state==EN_ICE)return EN_WATER;
  if(state==EN_SEED || state==EN_SPROUT || state==EN_TREE || state==EN_FOLIAGE)return EN_ASH;
 }
 if(action==EA_COLD && state==EN_WATER)return EN_ICE;
 return EN_NO_CHANGE;
}
