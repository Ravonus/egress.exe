#ifndef VIEW_CACHE_H
#define VIEW_CACHE_H
#include <gb/gb.h>
#include "core/eg_view.h"
/* 128x54 maximum backing: fourteen 288-byte rows per WRAM bank, banks 2..5.
 * Packets never cross DFFF, including the rightmost chunk of the last row.
 * Cache bank changes occur only in bounded leaves; packet must be fixed RAM. */
void view_store(void *ctx,uint8_t x,uint8_t y,uint8_t *packet) NONBANKED;
void view_load(void *ctx,uint8_t x,uint8_t y,uint8_t *packet) NONBANKED;
uint16_t view_address(uint8_t x,uint8_t y) NONBANKED;
uint8_t eg_view_shift_cached(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,int8_t dx,int8_t dy) NONBANKED;
uint8_t eg_view_shift_prepared(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,int8_t dx,int8_t dy) NONBANKED;
/* Internal raw leaves: caller masks interrupts, supplies fixed C000..CFFF
 * packets, and never crosses a 4 KiB bank boundary. Address 4000..5FFF is
 * metadata in banks 6/7; material rows occupy addresses below 4000. */
void view_put9(uint16_t address,const uint8_t *packet) __sdcccall(0) NONBANKED;
void view_get9(uint16_t address,uint8_t *packet) __sdcccall(0) NONBANKED;
#endif
