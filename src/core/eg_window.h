#ifndef EG_WINDOW_H
#define EG_WINDOW_H
#include "eg_room.h"
#define EW_WIDTH 48u
#define EW_STEP 4u
#define EW_PROTECTED 1u
#define EW_CRACKED 2u
#define EW_IDX(x,y) ((uint16_t)(((uint16_t)(y)<<6)|(uint8_t)(x)))
/* Four materials, four skins, then protected/cracked nibbles. Native callbacks live in ROM0; bank 1 must be selected on return.
 * The nine-byte packet must reside entirely in fixed C000..CFFF RAM. */
typedef void (*eg_window_io)(void *context,uint8_t x,uint8_t y,uint8_t *packet9);
extern uint8_t eg_window_packet[9];
#ifdef __SDCC
uint8_t eg_window_shift_cached(eg_room *r,uint8_t *skin,uint8_t *cracked,uint8_t *origin,int8_t direction) NONBANKED;
#endif
uint8_t eg_window_local(uint8_t origin,uint16_t global) EG_BANKED;
/* Move only the local material/presentation records. The caller rebases its
 * actor coordinates, named source metadata, queues and BG origin atomically
 * in the same game update. Overlapping cells retain their exact state. */
uint8_t eg_window_shift(eg_room *r,uint8_t *skin,uint8_t *cracked,uint8_t *origin,int8_t direction,
                       eg_window_io store,eg_window_io load,void *context) EG_BANKED;
#endif
