#include <gb/gb.h>
#include <gb/cgb.h>
#include "window_cases.h"
#include "wram_cache.h"
uint8_t __at(0xC400) result[4];
uint8_t __at(0xC600) output[WC_BYTES];
eg_room __at(0xD800) room;
uint8_t __at(0xD940) skin[EG_N];
uint8_t __at(0xDA20) cracked[28];
uint8_t __at(0xDDD0) canary[32];
static uint8_t origin;
uint8_t eg_window_packet[9];
void ew_store9(uint16_t offset,const void *p) __sdcccall(0) NONBANKED;
void ew_load9(uint16_t offset,void *p) __sdcccall(0) NONBANKED;
static const uint16_t row_offset[14]={0u,108u,216u,324u,432u,540u,648u,756u,864u,972u,1080u,1188u,1296u,1404u};
static void transfer(uint8_t put,uint8_t x,uint8_t y,uint8_t *p) NONBANKED {
  uint16_t off=row_offset[y]+(uint8_t)((x>>2)*9u);
  if(put && p!=eg_window_packet)memcpy(eg_window_packet,p,9u);
  CRITICAL {if(put)ew_store9(off,eg_window_packet);else ew_load9(off,eg_window_packet);}
  if(!put && p!=eg_window_packet)memcpy(p,eg_window_packet,9u);
  if((SVBK_REG&7u)!=1u)result[3]=4u;
}
void store(void *ctx,uint8_t x,uint8_t y,uint8_t *p) NONBANKED {(void)ctx;transfer(1u,x,y,p);}
void load(void *ctx,uint8_t x,uint8_t y,uint8_t *p) NONBANKED {(void)ctx;transfer(0u,x,y,p);}
void snapshot(uint8_t step) NONBANKED {wc_capture(&room,skin,cracked,origin,step,output);}
void main(void) {
  uint8_t step,i;
  DISPLAY_OFF;cpu_fast();SVBK_REG=1u;origin=0u;
  result[0]=result[1]=result[2]=result[3]=0u;
  if((uint16_t)eg_window_packet<0xC000u || (uint16_t)eg_window_packet>0xCFF7u)result[3]=5u;
  for(i=0u;i<32u;i++)canary[i]=0xA7u;
  wc_begin(&room,skin,cracked,store,0);
  for(step=0u;step<WC_STEPS;step++) {
    if(!wc_step(step,&room,skin,cracked,&origin,store,load,0))result[3]=1u;
    snapshot(step);
    for(i=0u;i<32u;i++)if(canary[i]!=0xA7u)result[3]=2u;
    result[1]=step;result[0]=0x5Au;
    while(!(joypad()&J_A))vsync();
    result[0]=0u;while(joypad()&J_A)vsync();
  }
  result[0]=0xA5u;while(1)vsync();
}
