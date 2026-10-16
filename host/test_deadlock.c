#include <assert.h>
#include <stdio.h>
#include "../src/core/eg_physics.h"
#include "../src/core/eg_piece.h"
int main(void) {
  eg_room r;uint8_t i,c=0,p=0;uint8_t tool=P_BRICK,mud=P_MUD;
  eg_physics_gen(&r,PH_PUSH,7919,0);
  for(i=0;i!=EG_N;i++) {if(r.c[i]==T_BLOCK)c=i;if(r.c[i]==T_PLATE)p=i;}
  assert(c && p);assert(eg_dead(&r,r.start,0,0)==0);
  r.c[c]=T_EMPTY;r.c[EG_IDX(14,11)]=T_BLOCK;
  assert(eg_dead(&r,r.start,0,0)!=0);
  assert(eg_dead(&r,r.start,&mud,1)!=0); /* mud cannot route around the gate through outer void */
  assert(eg_dead(&r,r.start,&tool,1)==0);
  r.c[EG_IDX(14,11)]=T_EMPTY;r.c[p]=T_BLOCK_PLATE;
  assert(eg_dead(&r,r.start,0,0)==0);
  puts("deadlock: stranded edge crate warns; live crate, alternate crafted weight and loaded plate remain valid");
}
