#ifndef EG_PHYSICS_H
#define EG_PHYSICS_H
#include "eg_room.h"
enum { PH_NONE = 0, PH_PUSH, PH_DROP, PH_BRIDGE, PH_PORTAL, PH_COUNT };
/* Physical motifs use the same board, gravity, pushes and reactions as play and the solver. */
void eg_physics_gen(eg_room *r, uint8_t motif, uint16_t seed, uint8_t level) EG_BANKED;
void eg_physics_goal(uint8_t motif, char *out) EG_BANKED;
void eg_portal_gen(eg_room *r,uint16_t seed,uint8_t level) EG_BANKED;
#endif
