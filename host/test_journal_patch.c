#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eg_journal.h"
int main(void){
 uint8_t p[256],copy[256];unsigned at,value;uint16_t state=0x1234u;
 memset(p,0,sizeof p);p[0]=3u;eg_save_seal(p,256u);
 for(at=0u;at<254u;at++)for(value=0u;value<256u;value++){
  memcpy(copy,p,256u);eg_save_patch256(copy,(uint8_t)at,(uint8_t)value);
  assert(copy[at]==value && eg_save_crc(copy,254u)==(uint16_t)(copy[254]|((uint16_t)copy[255]<<8)));
 }
 for(at=0u;at<10000u;at++){
  state=(uint16_t)(state*109u+89u);value=(state>>8)%254u;
  eg_save_patch256(p,(uint8_t)value,(uint8_t)state);
  assert(eg_save_crc(p,254u)==(uint16_t)(p[254]|((uint16_t)p[255]<<8)));
 }
 memcpy(copy,p,256u);eg_save_patch256(copy,254u,0u);eg_save_patch256(copy,255u,0u);assert(!memcmp(p,copy,256u));
 memset(p,0,112u);p[0]=1u;eg_save_seal(p,112u);
 for(at=0u;at<110u;at++)for(value=0u;value<256u;value++){
  memcpy(copy,p,112u);eg_save_patch112(copy,(uint8_t)at,(uint8_t)value);
  assert(copy[at]==value && eg_save_crc(copy,110u)==(uint16_t)(copy[110]|((uint16_t)copy[111]<<8)));
 }
 puts("Every byte/value patch and 10000 consecutive journal changes match a full CRC scan.");return 0;
}
