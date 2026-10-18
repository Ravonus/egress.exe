#include <assert.h>
#include <stdio.h>
#include "eg_room.h"
#include "eg_land.h"
#include "eg_stage.h"
#include "eg_physics.h"
int main(void){
 eg_room r;eg_land p;eg_stage stage;unsigned seed,tier,k,rooms=0u,worlds=0u,stages=0u;
 for(tier=0u;tier<6u;tier++)for(seed=1u;seed<=1024u;seed++)if(eg_generate(&r,(uint16_t)(seed*7919u),(uint8_t)tier)){
  for(k=0u;k<r.nitem;k++)assert(r.item_piece[k]>=1u && r.item_piece[k]<=5u);rooms++;
 }
 for(k=0u;k<EL_RULES;k++)for(seed=0u;seed<256u;seed++)if(eg_land_plan(&p,(uint16_t)(seed*997u),(uint8_t)k,255u,(uint8_t)(seed%8u))){
  for(tier=0u;tier<p.nitem;tier++)assert(p.item[tier].piece>=1u && p.item[tier].piece<=5u);worlds++;
 }
 for(tier=0u;tier<PH_COUNT;tier++)for(seed=0u;seed<32u;seed++){
  eg_physics_gen(&r,(uint8_t)tier,(uint16_t)seed,8u);for(k=0u;k<r.nitem;k++)assert(r.item_piece[k]>=1u && r.item_piece[k]<=5u);
 }
 /* Crafting waves expand each requested result back to its base reagents. */
 for(seed=0u;seed<128u;seed++)if(eg_stage_gen(&r,&stage,SG_MIXP,(uint16_t)(seed*997u),12u,255u)){
  for(k=0u;k<r.nitem;k++)assert(r.item_piece[k]>=1u && r.item_piece[k]<=5u);stages++;
 }
 assert(rooms>1000u && worlds>1000u && stages>100u);
 printf("Base-only map loot: %u ordinary rooms, %u seeded worlds, %u crafting-wave stages and all physical motifs.\n",rooms,worlds,stages);return 0;
}
