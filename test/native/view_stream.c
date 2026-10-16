#include <gb/gb.h>
#include <gb/cgb.h>
#include "view_cases.h"
#include "view_cache.h"
uint8_t __at(0xC400) result[4];
uint8_t __at(0xC600) output[VC_BYTES];
uint8_t eg_window_packet[9];
uint8_t __at(0xDDD0) canary[32];
eg_room __at(0xD800) room;
uint8_t __at(0xD940) skin[224];
uint8_t __at(0xDA20) cracked[28];
static eg_view view;
void snapshot(void) NONBANKED {vc_capture(&view,&room,skin,cracked,output);}
void main(void) {
  uint8_t which,step,i;
  DISPLAY_OFF;cpu_fast();SVBK_REG=1u;memset(result,0,4u);
  if((uint16_t)eg_window_packet<0xC000u || (uint16_t)eg_window_packet>0xCFF7u)result[3]=5u;
  for(i=0u;i<32u;i++)canary[i]=0xA7u;
  for(which=0u;which<VC_CASES;which++) {
    if(!vc_begin(which,&view,&room,skin,cracked,view_store,view_load))result[3]=1u;
    for(step=0u;step<VC_STEPS;step++) {
      if(!vc_step(&view,&room,skin,cracked,view_store,view_load))result[3]=2u;
      snapshot();
      if((SVBK_REG&7u)!=1u)result[3]=3u;
      for(i=0u;i<32u;i++)if(canary[i]!=0xA7u)result[3]=4u;
      result[1]=which;result[2]=step;result[0]=0x5Au;
      while(!(joypad()&J_A))vsync();
      result[0]=0u;while(joypad()&J_A)vsync();
    }
  }
  result[0]=0xA5u;while(1)vsync();
}
