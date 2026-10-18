#ifndef EG_COLLECTION_NATIVE_H
#define EG_COLLECTION_NATIVE_H
#include <gb/gb.h>
#include "core/eg_catalogue.h"
extern uint8_t collection_ready;
extern uint8_t __at(0xDDD8) collection_dirty;
void collection_load(void) BANKED;
void collection_pause(void) BANKED;
void collection_flush(void) BANKED;
uint8_t collection_mark(eg_item_id id) BANKED;
uint8_t collection_known(eg_item_id id) BANKED;
uint16_t collection_count(void) BANKED;
#endif
