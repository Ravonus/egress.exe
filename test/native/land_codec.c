#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "land_cases.h"
uint8_t __at(0xC400) result[4];
uint8_t __at(0xC410) header[EL_PLAN_BYTES];
uint8_t __at(0xC600) output[512];
uint8_t __at(0xDDD0) canary[32];
static eg_land plan;
void row(uint8_t y) NONBANKED {
  eg_land_cell cell;uint8_t x;uint8_t *out=output;
  for(x=0u;x<plan.width;x++) {
    eg_land_at(&plan,x,y,&cell);
    *out++=cell.material;*out++=cell.design;*out++=cell.variant;*out++=cell.flags;
  }
}
void main(void) {
  uint8_t k,y,i;
  DISPLAY_OFF;cpu_fast();SVBK_REG=1u;memset(result,0,4u);
  for(i=0u;i<32u;i++)canary[i]=0xA7u;
  for(k=0u;k<LC_CASES;k++) {
    if(!lc_plan(&plan,k)) {result[3]=1u;break;}
    memcpy(header,&plan,EL_PLAN_BYTES);
    for(y=0u;y<plan.height;y++) {
      row(y);
      if((SVBK_REG&7u)!=1u)result[3]=2u;
      for(i=0u;i<32u;i++)if(canary[i]!=0xA7u)result[3]=3u;
      result[1]=k;result[2]=y;result[0]=0x5Au;
      while(!(joypad()&J_A))vsync();
      result[0]=0u;while(joypad()&J_A)vsync();
    }
  }
  result[0]=0xA5u;while(1)vsync();
}
