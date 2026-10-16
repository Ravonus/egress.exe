#include <stdio.h>
#include "../test/native/link_cases.h"
int main(void) {
  eg_room r;uint8_t out[LC_BYTES],k,step;
  for(k=0u;k<LC_CASES;k++) {
    lc_begin(&r,k);
    for(step=0u;step<LC_STEPS;step++) {lc_step(&r,k,step,out);if(fwrite(out,1,sizeof out,stdout)!=sizeof out)return 1;}
  }
  return 0;
}
