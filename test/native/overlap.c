#include <gb/gb.h>
#include <gb/cgb.h>
#include "overlap.inc"
volatile uint8_t __at(0xC400) result[8];
static const uint8_t radii[10]={0,1,2,6,9,14,24,127,128,255};
static uint16_t rng=0xBEEF;
static uint8_t rnd(void) {rng=(uint16_t)(rng*25173u+13849u);return (uint8_t)(rng>>8);}
void main(void) {
  uint8_t a,b,r,rx,ry,x,y,dx,dy,want;uint16_t k;
  DISPLAY_OFF;cpu_fast();result[0]=result[1]=0u;
  for(r=0u;r!=10u;r++) {
    rx=radii[r];a=0u;
    do {b=0u;do {
      dx=a>b ? a-b : b-a;want=(uint8_t)(dx<rx);
      if(overlap(a,137u,b,137u,rx,1u)!=want || overlap(137u,a,137u,b,1u,rx)!=want) {
        result[0]=1u;result[2]=a;result[3]=b;result[4]=rx;while(1);
      }
    }while(++b);}while(++a);
    result[1]++;
  }
  for(k=0u;k!=4096u;k++) {
    a=rnd();b=rnd();x=rnd();y=rnd();rx=rnd();ry=rnd();
    dx=a>x ? a-x : x-a;dy=b>y ? b-y : y-b;want=(uint8_t)(dx<rx && dy<ry);
    if(overlap(a,b,x,y,rx,ry)!=want) {result[0]=2u;while(1);}
  }
  result[0]=0xA5u;while(1);
}
