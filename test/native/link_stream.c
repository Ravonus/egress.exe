#include <gb/gb.h>
#include <gb/cgb.h>
#include "link_cases.h"
uint8_t __at(0xC400) result[4];
uint8_t __at(0xC600) output[LC_BYTES];
eg_room __at(0xD800) fixture;
uint8_t __at(0xDDD0) canary[32];
void main(void) {
  uint8_t k,step,i;
  DISPLAY_OFF;cpu_fast();SVBK_REG=1u;
  for(i=0u;i<32u;i++)canary[i]=0xA7u;
  result[0]=result[1]=result[2]=result[3]=0u;
  for(k=0u;k<LC_CASES;k++) {
    lc_begin(&fixture,k);
    for(step=0u;step<LC_STEPS;step++) {
      lc_step(&fixture,k,step,output);
      for(i=0u;i<32u;i++)if(canary[i]!=0xA7u)result[3]=1u;
      if((SVBK_REG&7u)!=1u)result[3]=2u;
      result[1]=k;result[2]=step;result[0]=0x5Au;
      while(!(joypad()&J_A))vsync();
      result[0]=0u;while(joypad()&J_A)vsync();
    }
  }
  result[0]=0xA5u;
  while(1)vsync();
}
