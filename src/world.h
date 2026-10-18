#ifndef WORLD_H
#define WORLD_H
#include "core/eg_land_bind.h"
#include "view_cache.h"
extern eg_land world_plan;
extern eg_view world_view;
extern uint8_t world_on,world_latched,world_glitch;
extern uint8_t world_mask_packet[9],world_pre_x[9],world_pre_y[9];
uint8_t world_begin(uint16_t seed,uint8_t rule,uint8_t size,uint8_t biome) BANKED;
void world_show(void) BANKED;
void world_bind(void) BANKED;
uint8_t world_read(void *ctx,uint8_t x,uint8_t y) NONBANKED;
uint8_t world_write(uint8_t x,uint8_t y,uint8_t material) NONBANKED;
uint8_t world_read_skin(uint8_t x,uint8_t y) BANKED;
uint8_t world_write_skin(uint8_t x,uint8_t y,uint8_t material,uint8_t skin) BANKED;
uint8_t world_cached(uint8_t x,uint8_t y) NONBANKED;
void world_cache_load(void *ctx,uint8_t x,uint8_t y,uint8_t *packet) NONBANKED;
void world_mark_cached(uint8_t x,uint8_t y) NONBANKED;
uint8_t world_skin(uint8_t design,uint8_t variant) NONBANKED;
void world_clean_skin(uint8_t cell) NONBANKED;
void world_touch(uint8_t cell) NONBANKED;
void world_preserve(uint8_t x,uint8_t y) NONBANKED;
uint8_t world_preserved(int8_t dx,int8_t dy) NONBANKED;
uint8_t world_chunk_preserved(uint8_t x,uint8_t y) NONBANKED;
void world_sources(const uint8_t *occupied,uint8_t n) BANKED;
/* Material shift only. The play controller must rebase actors, camera and
 * presentation in the same update. Missing prefetched strips reject the move. */
uint8_t world_shift_data(int8_t dx,int8_t dy) BANKED;
uint8_t world_shift_prepared(int8_t dx,int8_t dy) BANKED;
void world_prepare_binding(int8_t dx,int8_t dy) BANKED;
uint8_t world_binding_ready(int8_t dx,int8_t dy) NONBANKED;
#endif
