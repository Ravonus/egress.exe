#include <stdio.h>
#include "../test/native/window_cases.h"
uint8_t eg_window_packet[9];
static uint8_t cache[EG_H][EW_WIDTH/4u][9];
static void store(void *ctx,uint8_t x,uint8_t y,uint8_t *p) {(void)ctx;memcpy(cache[y][x>>2],p,9u);}
static void load(void *ctx,uint8_t x,uint8_t y,uint8_t *p) {(void)ctx;memcpy(p,cache[y][x>>2],9u);}
int main(void) {
  eg_room r;uint8_t skin[EG_N],crack[28],out[WC_BYTES],origin=0u,step;
  wc_begin(&r,skin,crack,store,0);
  for(step=0u;step<WC_STEPS;step++) {
    if(!wc_step(step,&r,skin,crack,&origin,store,load,0))return 2;
    wc_capture(&r,skin,crack,origin,step,out);
    if(fwrite(out,1,sizeof out,stdout)!=sizeof out)return 1;
  }
  return 0;
}
