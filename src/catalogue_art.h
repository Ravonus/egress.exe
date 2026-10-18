#ifndef EG_CATALOGUE_ART_H
#define EG_CATALOGUE_ART_H
#include <gb/gb.h>
#include "core/eg_catalogue.h"
void catalogue_art_raw(eg_item_id id,uint8_t pose,uint8_t *out) BANKED;
void catalogue_art_upload(eg_item_id id,uint8_t state,uint8_t pose,uint8_t tile) BANKED;
void catalogue_art_palette(eg_item_id id,uint8_t slot,uint8_t obj) BANKED;
void catalogue_art_ui_palettes(void) BANKED;
uint8_t catalogue_art_bg_palette(eg_item_id id) BANKED;
void catalogue_bench_body(eg_item_id id,uint8_t pose,uint8_t tile) BANKED;
void catalogue_fusion_begin(eg_item_id a,eg_item_id b,eg_item_id result) BANKED;
void catalogue_fusion_step(uint8_t step,uint8_t tile) BANKED;
#endif
