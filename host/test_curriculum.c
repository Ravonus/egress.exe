#include <assert.h>
#include <stdio.h>
#include "../src/core/eg_family.h"
#include "../src/core/eg_stage.h"
int main(void) {
  eg_family f;eg_plan_state p;eg_slice sl;eg_seed_rec rec;
  unsigned seed,g,floor;uint16_t introduced;
  for(seed=1;seed<=64;seed++) {
    eg_family_init(&f,(uint16_t)seed);introduced=MK_BASIC;
    for(g=0;g<6;g++) {
      eg_plan_begin(&p,(uint16_t)(seed*7919u+g),f.seen,(uint8_t)g);eg_slice_begin(&sl);
      for(floor=1;floor<=10;floor++) {
        eg_slice_floor(&sl,&p,(uint8_t)floor,10,&rec,255u);
        if(floor==2) assert(sl.type==SG_HOLDOUT && !sl.planned);
        if(floor==1 || floor==3 || floor==7) assert(sl.physical && !sl.planned);
        if(sl.planned) {
          if(p.mech_new!=255u) introduced|=(uint16_t)(1u<<p.mech_new);
          if(!g && floor==4) assert(rec.trick==TRICK_SHORT);
          if(!g && floor==9) assert(rec.trick==TRICK_COLD);
          eg_plan_result(&p,0u,1800u);f.seen|=p.seen;
        }
      }
      eg_family_choose(&f,0u);eg_family_finish(&f,10u,1u);
    }
    assert((introduced&0x1fffu)==0x1fffu && f.seen==introduced);
  }
  puts("curriculum: 64 families, all 13 mechanics reached within six generations; physical lessons and early waves present");
  return 0;
}
