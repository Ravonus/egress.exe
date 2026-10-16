#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "gen/look_art.h"
#include "gen/world_art.h"
uint8_t __at(0xC400) result[4];
uint8_t __at(0xDDD0) canary[32];
uint8_t tile[M_COUNT*4u],pal[M_COUNT];
uint16_t bg[32];
static const uint8_t biome[4]={0u,1u,3u,7u};
void ready(uint8_t style,uint8_t frame,uint8_t initial) NONBANKED {
  uint8_t i;
  if((SVBK_REG&7u)!=1u || (VBK_REG&1u))result[3]=1u;
  for(i=0u;i<32u;i++)if(canary[i]!=0xA7u)result[3]=2u;
  result[1]=style;result[2]=frame;result[0]=initial ? 0x5Bu : 0x5Au;
  while(!(joypad()&J_A))vsync();
  result[0]=0u;while(joypad()&J_A)vsync();
}
void main(void) {
  uint8_t style,i,frame;
  DISPLAY_OFF;cpu_fast();SVBK_REG=1u;LCDC_REG&=(uint8_t)~LCDCF_BG8000;memset(result,0,4u);
  for(i=0u;i<32u;i++)canary[i]=0xA7u;
  for(style=0u;style<4u;style++) {
    look_art_load(biome[style],bg,pal,tile);
    world_art_load(style,pal,tile,bg);ready(style,0u,1u);
    frame=0u;
    do {world_art_tick(tile,frame);ready(style,frame,0u);}while(++frame);
  }
  result[0]=0xA5u;while(1)vsync();
}
