#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_combat.h"
int main(void) {
  eg_room r; uint8_t p=EG_IDX(3,5); unsigned a,b,s;
  for(a=0;a<EG_TAGS;a++) for(b=0;b<EG_TAGS;b++) {
    assert(eg_combat_reaction(TG(a),(uint8_t)b,0,0)==eg_rx((uint8_t)a,(uint8_t)b));
    for(s=0;s<256;s++) {
      uint8_t rx=eg_combat_reaction(TG(a),(uint8_t)b,(uint8_t)s,0);
      assert(rx<RX_COUNT);
      if(a==TAG_COLD && (s&MS_WET)) assert(rx==RX_FREEZE);
      if(a==TAG_HOT && (s&MS_FROZEN)) assert(rx==RX_THAW);
    }
  }
  for(b=0;b<EG_TAGS;b++) assert(eg_combat_reaction(0,(uint8_t)b,MS_WET,1)==RX_SHORT);
  memset(&r,0,sizeof r);r.c[p+1]=T_BLOCK;r.c[p+2]=T_PLATE;
  assert(eg_use_rx(P_AIR,T_BLOCK)==RX_PUSH);
  assert(eg_apply(&r,P_AIR,p+1,&p,1)==RX_PUSH && r.c[p+1]==T_EMPTY && r.c[p+2]==T_BLOCK_PLATE);
  r.c[p+3]=T_WALL;
  assert(eg_apply(&r,P_AIR,p+2,&p,1)==RX_NONE && r.c[p+2]==T_BLOCK_PLATE);
  r.c[p+1]=T_PIT;
  assert(eg_apply(&r,P_FIRE,p+1,&p,1)==RX_NONE && r.c[p+1]==T_PIT);
  assert(eg_apply(&r,P_MUD,p+1,&p,1)==RX_MUD && r.c[p+1]==T_EMPTY);
  puts("interaction matrix: 16384 status/element cases, shock, air load and blocked push, mud crossing, invalid tool");
  return 0;
}
