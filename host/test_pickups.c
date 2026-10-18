#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eg_room.h"
#include "eg_land.h"
#include "eg_piece.h"
int main(void){
 eg_room r;eg_land p;eg_land_cell c;unsigned seed,x,y,k,n,total=0u;uint8_t regions[256],before[EG_N];
 memset(&r,0,sizeof r);r.start=34u;r.door=8u;r.c[r.door]=T_DOOR;r.c[51]=T_ITEM;r.prot[8]=255u;
 for(seed=0u;seed<256u;seed++){
  r.c[34]=T_EMPTY;for(k=0u;k<EG_N;k++)if(EG_HEALTH(r.c[k]))r.c[k]=T_EMPTY;
  memcpy(before,r.c,EG_N);eg_health_room(&r,(uint16_t)seed);n=0u;
  for(k=0u;k<EG_N;k++)if(EG_HEALTH(r.c[k])){n++;assert(k!=r.start && before[k]==T_EMPTY && !EG_PROT(&r,k));}
  assert(n==1u);memcpy(before,r.c,EG_N);eg_health_room(&r,(uint16_t)(seed+7u));assert(!memcmp(before,r.c,EG_N));
 }
 for(seed=0u;seed<128u;seed++){
  assert(eg_land_plan(&p,(uint16_t)seed,(uint8_t)(seed%6u),255u,(uint8_t)(seed%8u)));memset(regions,0,sizeof regions);
  for(y=0u;y<p.height;y++)for(x=0u;x<p.width;x++){
   eg_land_at(&p,(uint8_t)x,(uint8_t)y,&c);if(!EG_HEALTH(c.material))continue;
   assert(!(c.flags&EL_PROTECTED));k=(y>>4)*16u+(x>>4);assert(!regions[k]);regions[k]=1u;total++;
  }
 }
 assert(total>0u);assert(EG_HEALTH(T_TOKEN|F_HEALTH|F_OB));assert(!EG_HEALTH(T_TOKEN) && !EG_HEALTH(T_WALL|F_BENCH));
 memset(&r,0,sizeof r);r.c[51]=T_TOKEN|F_HEALTH;r.c[35]=T_ROCK;r.c[67]=T_WALL;
 eg_gravity(&r,0,0u);assert((r.c[51]&T_MASK)==T_ROCK); /* medicine cannot prop up a puzzle load */
 memset(&r,0,sizeof r);r.c[51]=T_TOKEN|F_HEALTH;assert(eg_apply(&r,P_BRICK,51u,0,0u)==RX_WEIGHT && r.c[51]==T_BLOCK);
 r.c[51]=T_BLOCK;r.c[52]=T_TOKEN|F_HEALTH;assert(eg_push_block(&r,51u,52u) && r.c[52]==T_BLOCK);
 printf("Medicine preserves apparatus and protected cells, never duplicates on constructor retries; %u seeded world pickups obey one-per-region/view-pool bounds.\n",total);return 0;
}
