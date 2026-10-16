#include <stdio.h>
#include <string.h>
#include "../test/native/land_bind_cases.h"
int main(void) {
  lb_context ctx;eg_room r;
  for(unsigned k=0;k<LC_CASES;k++) {
    if(!lc_plan(&ctx.p,k))return 1;
    for(unsigned step=0;step<16;step++) {lb_step(&ctx,&r,step);if(fwrite(&r,1,320,stdout)!=320)return 2;}
  }
  return 0;
}
