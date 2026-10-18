#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_bag.h"
static eg_item_id get(const uint8_t *lo,const uint8_t *hi,uint8_t slot) {
  return EG_BAG_VALUE(lo,hi,slot);
}
static void put(uint8_t *lo,uint8_t *hi,uint8_t slot,eg_item_id id) {
  uint8_t mask=(uint8_t)(1u<<(slot&7u));lo[slot]=(uint8_t)id;
  if(id&256u)hi[slot>>3]|=mask;else hi[slot>>3]&=(uint8_t)~mask;
}
eg_item_id eg_bag_get(const uint8_t *lo,const uint8_t *hi,uint8_t slot) EG_BANKED {return slot<24u ? get(lo,hi,slot) : 0u;}
uint8_t eg_bag_put(uint8_t *lo,uint8_t *hi,uint8_t slot,eg_item_id id) EG_BANKED {
  if(slot>=24u || id>444u)return 0u;put(lo,hi,slot,id);return 1u;
}
uint8_t eg_bag_remove(uint8_t *lo,uint8_t *hi,uint8_t *count,uint8_t slot) EG_BANKED {
  uint8_t k;if(*count>24u || slot>=*count)return 0u;
  for(k=slot;k+1u<*count;k++)put(lo,hi,k,get(lo,hi,(uint8_t)(k+1u)));
  put(lo,hi,--*count,0u);return 1u;
}
uint8_t eg_bag_valid(const uint8_t *lo,const uint8_t *hi,uint8_t count) EG_BANKED {
  uint8_t k;eg_item_id id;if(count>24u)return 0u;
  for(k=0u;k<24u;k++){id=get(lo,hi,k);if(k<count ? (!id || id>444u) : (id!=0u))return 0u;}
  return 1u;
}
