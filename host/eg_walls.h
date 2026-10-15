#ifndef EG_WALLS_H
#define EG_WALLS_H
#include "../src/core/eg_room.h"
typedef struct {
  int cells, comps, single, single_float, small, anchored;
} eg_wall_stats;
void eg_walls(const eg_room *r, eg_wall_stats *s);
#endif
