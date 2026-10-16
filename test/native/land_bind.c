#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "land_bind_cases.h"
uint8_t __at(0xC400) result[4];
uint8_t __at(0xC600) output[320];
uint8_t __at(0xDDD0) canary[32];
eg_room __at(0xD800) room;
static lb_context context;
void main(void) {
  uint8_t k,step,i;
  DISPLAY_OFF;cpu_fast();SVBK_REG=1u;memset(result,0,4u);
  for(i=0u;i<32u;i++)canary[i]=0xA7u;
  for(k=0u;k<LC_CASES;k++) {
    if(!lc_plan(&context.p,k))result[3]=1u;
    for(step=0u;step<16u;step++) {
      lb_step(&context,&room,step);memcpy(output,&room,320u);
      if((SVBK_REG&7u)!=1u)result[3]=2u;
      for(i=0u;i<32u;i++)if(canary[i]!=0xA7u)result[3]=3u;
      result[1]=k;result[2]=step;result[0]=0x5Au;
      while(!(joypad()&J_A))vsync();
      result[0]=0u;while(joypad()&J_A)vsync();
    }
  }
  result[0]=0xA5u;while(1)vsync();
}
