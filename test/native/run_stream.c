#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "core/eg_plan.h"
#include "wram_cache.h"
#include "run_cases.h"
static eg_world __at(0xC410) world;
static eg_run_in __at(0xC450) input;
static eg_slot __at(0xC480) scratch;
static eg_slot __at(0xC800) slots[EG_RUN_SLOTS];
/* Match the game's actual 560-byte live-stack boundary. */
static uint8_t __at(0xDDD0) live[32];
volatile uint8_t __at(0xC400) result[4];
static void take(uint8_t index,const eg_slot *slot,void *context) NONBANKED {
  context;
  if(index!=result[2] || index>=EG_RUN_SLOTS ||
     !wram_put(7u,(uint16_t)index*sizeof(eg_slot),slot,sizeof(eg_slot))) result[3]=1u;
  result[2]++;
}
void main(void) {
  uint8_t k,j,n;
  SVBK_REG=1u;cpu_fast();memset((void *)result,0,4u);
  for(j=0u;j!=32u;j++)live[j]=(uint8_t)(j^0xA7u);
  for(k=0u;k!=RUN_CASES;k++) {
    run_case(&input,k);result[2]=0u;
    n=eg_run_stream(&world,&scratch,&input,take,0);
    if(!n || n!=result[2] || result[3]) {result[0]=1u;break;}
    if(!wram_get(slots,7u,0u,(uint16_t)n*sizeof(eg_slot))) {result[0]=2u;break;}
    if((SVBK_REG&7u)!=1u) {result[0]=3u;break;}
    for(j=0u;j!=32u;j++)if(live[j]!=(uint8_t)(j^0xA7u))result[0]=4u;
    if(result[0])break;
    result[1]=k;result[0]=0x5Au;
    /* Host reads an actual completed constructor, then normal input releases
     * this checkpoint. No injected calls or CPU-register patching. */
    while(!(joypad()&J_A))vsync();
    while(joypad()&J_A)vsync();
    result[0]=0u;
  }
  if(!result[0])result[0]=0xA5u;
  while(1)vsync();
}
