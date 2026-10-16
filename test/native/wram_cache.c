#include <gb/gb.h>
#include <gb/cgb.h>
#include "wram_cache.h"
static uint8_t __at(0xC800) src[1024];
static uint8_t __at(0xCC00) dst[1024];
static uint8_t __at(0xD200) live[64];
volatile uint8_t __at(0xC400) result[4];
static uint8_t pattern(uint8_t sector,uint16_t i) {return (uint8_t)((i*37u)^(i>>3)^(sector*61u));}
void main(void) {
  uint8_t s,k;uint16_t i;
  SVBK_REG=1u;cpu_fast();result[0]=0u;result[1]=0u;
  for(k=0;k!=64u;k++) live[k]=(uint8_t)(k^0xA7u);
  for(s=0;s!=WRAM_SECTORS;s++) {
    for(i=0;i!=1024u;i++) src[i]=pattern(s,i);
    if(!wram_put(s,0u,src,1024u)) {result[0]=1u;return;}
  }
  for(s=0;s!=WRAM_SECTORS;s++) {
    if(!wram_get(dst,s,0u,1024u)) {result[0]=2u;return;}
    for(i=0;i!=1024u;i++) if(dst[i]!=pattern(s,i)) {result[0]=3u;result[2]=s;return;}
    for(k=0;k!=64u;k++) if(live[k]!=(uint8_t)(k^0xA7u)) {result[0]=4u;return;}
    if((SVBK_REG&7u)!=1u) {result[0]=5u;return;}
    result[1]++;
  }
  for(s=0;s!=WRAM_SECTORS;s++) {
    for(i=0;i!=71u;i++) src[i]=(uint8_t)(s+i);
    if(!wram_put(s,239u,src,71u) || !wram_get(dst,s,239u,71u)) {result[0]=6u;return;}
    for(i=0;i!=71u;i++) if(dst[i]!=(uint8_t)(s+i)) {result[0]=7u;return;}
    result[1]++;
  }
  if(wram_put(8u,0u,src,1u) || wram_get(dst,0u,1023u,2u) || wram_put(0u,0u,src,1025u)) {result[0]=8u;return;}
  result[0]=0xA5u;while(1) vsync();
}
