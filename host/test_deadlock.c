#include <assert.h>
#include <stdio.h>
#include "../src/core/eg_physics.h"
#include "../src/core/eg_piece.h"
static void sliced(const eg_room *r,const uint8_t *tools,uint8_t n,uint8_t expected) {
  unsigned budget,calls;uint8_t result;
  for(budget=1u;budget<=7u;budget++) {
    eg_dead_begin(r->start);assert(eg_dead_step(r,tools,n,0u)==EG_DEAD_BUSY);
    calls=0u;
    do {result=eg_dead_step(r,tools,n,(uint8_t)budget);assert(++calls<2000u);} while(result==EG_DEAD_BUSY);
    assert(result==expected);
  }
}
int main(void) {
  eg_room r;uint8_t i,c=0,p=0;uint8_t tool=P_BRICK,mud=P_MUD;
  eg_physics_gen(&r,PH_PUSH,7919,0);
  for(i=0;i!=EG_N;i++) {if(r.c[i]==T_BLOCK)c=i;if(r.c[i]==T_PLATE)p=i;}
  assert(c && p);assert(eg_dead(&r,r.start,0,0)==0);
  sliced(&r,0,0,0);
  r.c[c]=T_EMPTY;r.c[EG_IDX(14,11)]=T_BLOCK;
  assert(eg_dead(&r,r.start,0,0)!=0);
  sliced(&r,0,0,eg_dead(&r,r.start,0,0));
  assert(eg_dead(&r,r.start,&mud,1)!=0); /* mud cannot route around the gate through outer void */
  assert(eg_dead(&r,r.start,&tool,1)==0);
  sliced(&r,&tool,1,0);
  r.c[EG_IDX(14,11)]=T_EMPTY;r.c[p]=T_BLOCK_PLATE;
  assert(eg_dead(&r,r.start,0,0)==0);
  sliced(&r,0,0,0);
  puts("deadlock: stranded edge crate warns; live crate, alternate crafted weight and loaded plate remain valid");
}
