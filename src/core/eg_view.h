#ifndef EG_VIEW_H
#define EG_VIEW_H
#include "eg_room.h"
/* A bounded 16x14 simulation window over a mutable world, in four-cell steps.
 * Backing packets contain four materials, four skins, then protected/cracked
 * nibbles. Coordinates passed to I/O are global; x is a multiple of four. */
typedef struct {uint8_t x,y,width,height;} eg_view;
typedef void (*eg_view_io)(void *context,uint8_t x,uint8_t y,uint8_t *packet9);
extern uint8_t eg_window_packet[9];
uint8_t eg_view_valid(const eg_view *v) EG_BANKED;
uint8_t eg_view_rebase(uint8_t cell,int8_t dx,int8_t dy) EG_RNG_ABI;
void eg_view_bits(uint8_t *bits,int8_t dx,int8_t dy,uint8_t fill) EG_RNG_ABI;
uint8_t eg_view_local(const eg_view *v,uint8_t x,uint8_t y) EG_BANKED;
uint8_t eg_view_load(const eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,
                     eg_view_io load,void *context) EG_BANKED;
uint8_t eg_view_flush(const eg_view *v,const eg_room *r,const uint8_t *skin,const uint8_t *cracked,
                      eg_view_io store,void *context) EG_BANKED;
uint8_t eg_view_shift(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,int8_t dx,int8_t dy,
                      eg_view_io store,eg_view_io load,void *context) EG_BANKED;
/* A NULL store means the caller has already persisted every outgoing packet.
 * Incoming packets are still loaded and all active fields move identically. */
#endif
