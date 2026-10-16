/* Exact single-map oracle for input-driven native world traversal. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/core/eg_land.h"
int main(int argc,char **argv) {
  eg_land p;eg_land_cell c;
  if(argc!=5 || !eg_land_plan(&p,(uint16_t)strtoul(argv[1],0,0),
      (uint8_t)strtoul(argv[2],0,0),(uint8_t)strtoul(argv[3],0,0),(uint8_t)strtoul(argv[4],0,0)))return 1;
  if(fwrite(&p,1,EL_PLAN_BYTES,stdout)!=EL_PLAN_BYTES)return 1;
  for(unsigned y=0;y<p.height;y++)for(unsigned x=0;x<p.width;x++) {
    eg_land_at(&p,(uint8_t)x,(uint8_t)y,&c);
    if(fwrite(&c,1,4,stdout)!=4)return 1;
  }
  return 0;
}
