#include <stdio.h>
#include <stddef.h>
#include "../test/native/land_cases.h"
typedef char land_header_layout[(offsetof(eg_land,node)==11u && sizeof(eg_land_node)==6u && sizeof(eg_land_item)==3u) ? 1 : -1];
int main(void) {
  eg_land p;eg_land_cell c;
  for(unsigned k=0;k<LC_CASES;k++) {
    if(!lc_plan(&p,(uint8_t)k))return 1;
    if(fwrite(&p,1,EL_PLAN_BYTES,stdout)!=EL_PLAN_BYTES)return 1;
    for(unsigned y=0;y<p.height;y++)for(unsigned x=0;x<p.width;x++) {
      eg_land_at(&p,(uint8_t)x,(uint8_t)y,&c);
      if(fwrite(&c,1,4,stdout)!=4)return 1;
    }
  }
  return 0;
}
