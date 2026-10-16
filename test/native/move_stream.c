#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "move_cases.h"
static uint8_t __at(0xC800) cells[1024];
static uint8_t __at(0xCC00) map[256];
static eg_mover_t __at(0xC600) batch[MOVE_BATCH];
static uint8_t __at(0xDDD0) canary[32];
volatile uint8_t __at(0xC400) result[4];
typedef char native_move_record[(sizeof(eg_mover_t)==22u) ? 1 : -1];
void main(void) {
  uint8_t k,f,b,j;
  cpu_fast();SVBK_REG=1u;memset((void *)result,0,4u);memset(canary,0xA7u,32u);
  for(k=0u;k!=MOVE_CASES;k++) {
    move_case(k,cells,map);
    for(b=0u;b!=MOVE_STEPS/MOVE_BATCH;b++) {
      for(j=0u;j!=MOVE_BATCH;j++) {f=(uint8_t)(b*MOVE_BATCH+j);move_sample(k,f);batch[j]=mc_mover;}
      for(j=0u;j!=32u;j++)if(canary[j]!=0xA7u)result[3]=1u;
      result[1]=k;result[2]=b;result[0]=0x5Au;
      while(!(joypad()&J_A))vsync();while(joypad()&J_A)vsync();result[0]=0u;
    }
  }
  result[0]=0xA5u;while(1)vsync();
}
