#ifndef EG_LAND_DEAD_H
#define EG_LAND_DEAD_H
#include "eg_land_bind.h"
/* Sound, conservative certificates for the current world grammar. Unknown
 * geometry is optimistic; no scan generates an unseen terrain chunk. Positive
 * answers mean a route remains possible, not that every action sequence wins. */
#define EL_DEAD_BYTES 36u
typedef struct {uint8_t b[EL_DEAD_BYTES];} eg_land_dead;
typedef struct {
  uint8_t (*known)(void *ctx,uint8_t x,uint8_t y);
  uint8_t (*read)(void *ctx,uint8_t x,uint8_t y);
  void (*quad)(void *ctx,uint8_t x,uint8_t y,uint8_t *out4);
  uint8_t (*mask)(void *ctx,uint8_t row_pair,uint8_t byte);
  void *ctx;
} eg_land_dead_io;
void eg_land_dead_begin(eg_land_dead *state) EG_BANKED;
uint8_t eg_land_dead_step(eg_land_dead *state,const eg_land *p,const eg_view *view,const eg_room *r,
                          uint8_t from,const uint8_t *bag,uint8_t nbag,uint8_t latched,
                          const eg_land_dead_io *io,uint8_t budget) EG_BANKED;
#endif
