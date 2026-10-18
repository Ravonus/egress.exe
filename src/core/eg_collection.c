#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_collection.h"
#include "eg_journal.h"
void eg_collection_empty(uint8_t *p) EG_BANKED {
  memset(p,0,64u);p[0]=EG_COLLECTION_VERSION;eg_save_seal(p,64u);
}
uint8_t eg_collection_valid(const uint8_t *p) EG_BANKED {
  return (uint8_t)(eg_save_valid(p,64u,EG_COLLECTION_VERSION) && !(p[59]&240u) && !p[60] && !p[61] &&
    eg_discovery_count(p+4u)==(uint16_t)(p[2]|((uint16_t)p[3]<<8)));
}
uint8_t eg_collection_choose(const uint8_t *a,const uint8_t *b) EG_BANKED {
  uint8_t va=eg_collection_valid(a),vb=eg_collection_valid(b);
  if(!va && !vb)return 255u;
  return (uint8_t)(vb && (!va || !eg_save_newer(a[1],b[1])));
}
uint8_t eg_collection_mark(uint8_t *p,eg_item_id id) EG_BANKED {
  if(!eg_discovery_mark(p+4u,id))return 0u;
  if(!++p[2])p[3]++;
  return 1u;
}
