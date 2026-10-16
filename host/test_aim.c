#include <assert.h>
#include <stdio.h>
#include "../src/core/eg_move.h"
int main(void) {
  int dx,dy;unsigned k,n=0;int8_t vx,vy;
  const unsigned speeds[]={0,7,18,20,22,36,44,63};
  for(dx=-255;dx<=255;dx+=3) for(dy=-255;dy<=255;dy+=3) for(k=0;k<8;k++) {
    int ax=dx<0 ? -dx : dx,ay=dy<0 ? -dy : dy,m=ax>ay ? ax : ay;
    if(!m) m=1;
    eg_aim((int16_t)dx,(int16_t)dy,(uint8_t)speeds[k],&vx,&vy);
    assert(vx==(int8_t)(dx*(int)speeds[k]/m) && vy==(int8_t)(dy*(int)speeds[k]/m));n++;
  }
  printf("aim: %u exact velocity comparisons, including the five-page boss fan and heir fire speeds\n",n);
  return 0;
}
