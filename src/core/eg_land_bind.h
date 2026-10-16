#ifndef EG_LAND_BIND_H
#define EG_LAND_BIND_H
#include "eg_land.h"
#include "eg_view.h"
typedef uint8_t (*eg_land_read)(void *context,uint8_t x,uint8_t y);
typedef struct {
  uint8_t start,door,nitem,item_cell[EG_MAX_ITEM],item_piece[EG_MAX_ITEM];
  uint8_t nlink;eg_link link[EG_LINKS];uint8_t portal[2],portal_need;
} eg_land_frame;
typedef char eg_land_frame_layout[(sizeof(eg_land_frame)==27u) ? 1 : -1];
void eg_land_bind_frame(const eg_land *p,const eg_view *v,eg_land_frame *out,uint8_t latched,
                        eg_land_read read,void *context) EG_BANKED;
void eg_land_apply_frame(eg_room *room,const eg_land_frame *frame,uint8_t latched) EG_BANKED;
/* Bind global identities to a local simulation window. Never rebuild the
 * material/protection board here. Inactive source values come from the mutable
 * backing world, not the seed; latch bits belong to this stage's live state. */
void eg_land_bind(const eg_land *p,const eg_view *v,eg_room *r,uint8_t latched,
                  eg_land_read read,void *context) EG_BANKED;
uint8_t eg_land_water_style(uint8_t biome) EG_BANKED;
#endif
