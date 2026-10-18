#ifndef EG_COLLECTION_H
#define EG_COLLECTION_H
#include "eg_catalogue.h"
#define EG_COLLECTION_VERSION 1u
#define EG_COLLECTION_RECORD 64u
#define EG_COLLECTION_BITS 4u
void eg_collection_empty(uint8_t *record) EG_BANKED;
uint8_t eg_collection_valid(const uint8_t *record) EG_BANKED;
uint8_t eg_collection_choose(const uint8_t *a,const uint8_t *b) EG_BANKED; /* 0/1, 255 neither */
uint8_t eg_collection_mark(uint8_t *record,eg_item_id id) EG_BANKED;
#endif
