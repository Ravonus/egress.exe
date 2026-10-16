#include <stdio.h>
#include "../test/native/view_cases.h"
uint8_t eg_window_packet[9];
static uint8_t cache[54][32][9];
static void store(void *ctx,uint8_t x,uint8_t y,uint8_t *p) {(void)ctx;memcpy(cache[y][x>>2],p,9u);}
static void load(void *ctx,uint8_t x,uint8_t y,uint8_t *p) {(void)ctx;memcpy(p,cache[y][x>>2],9u);}
int main(void) {
  eg_view v;eg_room r;uint8_t skin[224],crack[28],output[VC_BYTES];
  for(unsigned which=0;which<VC_CASES;which++) {
    if(!vc_begin(which,&v,&r,skin,crack,store,load))return 1;
    for(unsigned step=0;step<VC_STEPS;step++) {
      if(!vc_step(&v,&r,skin,crack,store,load))return 2;
      vc_capture(&v,&r,skin,crack,output);if(fwrite(output,1,sizeof output,stdout)!=sizeof output)return 3;
    }
  }
  return 0;
}
