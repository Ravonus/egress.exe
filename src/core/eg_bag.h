#ifndef EG_BAG_H
#define EG_BAG_H
#include "eg_catalogue.h"
#define EG_BAG_CAP 24u
#define EG_BAG_HIGH_BYTES 3u
#define EG_BAG_VALUE_MASK(lo,hi,k,mask) ((eg_item_id)((lo)[k]|(((hi)[(k)>>3]&(mask)) ? 256u : 0u)))
#define EG_BAG_VALUE(lo,hi,k) EG_BAG_VALUE_MASK(lo,hi,k,(uint8_t)(1u<<((k)&7u)))
/* 24 low bytes and three high-bit bytes, exactly 27 bytes per checkpoint. */
eg_item_id eg_bag_get(const uint8_t *lo,const uint8_t *hi,uint8_t slot) EG_BANKED;
uint8_t eg_bag_put(uint8_t *lo,uint8_t *hi,uint8_t slot,eg_item_id id) EG_BANKED;
uint8_t eg_bag_remove(uint8_t *lo,uint8_t *hi,uint8_t *count,uint8_t slot) EG_BANKED;
uint8_t eg_bag_valid(const uint8_t *lo,const uint8_t *hi,uint8_t count) EG_BANKED;
#endif
