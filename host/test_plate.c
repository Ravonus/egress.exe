#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_room.h"
int main(void) {
  eg_room r;uint8_t p=EG_IDX(7,8),g=EG_IDX(7,5),away=EG_IDX(6,8);
  memset(&r,0,sizeof r);r.c[p]=T_PLATE;r.c[g]=T_GATE;
  eg_plates_dirty=1;eg_shutters(&r,&p,1);assert(r.c[g]==T_GATE_OPEN);
  eg_plates_dirty=1;eg_shutters(&r,&away,1);assert(r.c[g]==T_GATE);
  eg_plates_dirty=1;eg_shutters(&r,&p,1);assert(r.c[g]==T_GATE_OPEN);
  eg_plates_dirty=1;eg_shutters(&r,&g,1);assert(r.c[g]==T_GATE_OPEN);
  eg_plates_dirty=1;eg_shutters(&r,0,0);assert(r.c[g]==T_GATE);
  r.c[p]=T_BLOCK_PLATE;eg_plates_dirty=1;eg_shutters(&r,&away,1);assert(r.c[g]==T_GATE_OPEN);
  puts("plates: player weight, release, occupied-gate safety, airborne exclusion and crate weight pass");return 0;
}
