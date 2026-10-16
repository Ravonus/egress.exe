#include <stdio.h>
#include <string.h>
#include "../test/native/run_cases.h"
int main(void) {
  eg_run_in in;eg_run run;uint8_t k;
  for(k=0u;k!=RUN_CASES;k++) {
    run_case(&in,k);memset(&run,0,sizeof run);eg_run_build(&run,&in);
    if(fwrite(&run,1,sizeof(eg_world),stdout)!=sizeof(eg_world) ||
       fwrite(run.s,sizeof(eg_slot),run.n,stdout)!=run.n)return 1;
  }
  return 0;
}
