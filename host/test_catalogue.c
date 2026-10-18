#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "eg_catalogue.h"
static uint16_t cost[445],depth[445],incoming[445];
int main(void) {
  eg_item_id id,a,b,r;uint16_t n=0,passes=0,recipes=0;uint8_t bits[56]={0},seen[445]={0};
  char names[445][19],name[19];eg_item_profile p;
  for(id=1;id<=444;id++) {
    eg_catalogue_name(id,name);assert(strlen(name)>0 && strlen(name)<=18);strcpy(names[id],name);
    for(a=1;a<id;a++)assert(strcmp(names[a],name));
    eg_catalogue_profile(id,&p);assert(p.family>=1 && p.family<=24 && p.effect>=1 && p.effect<=16 && p.cost<=40 && p.power>=1 && p.power<=4);
    assert(eg_discovery_mark(bits,id));assert(!eg_discovery_mark(bits,id));assert(eg_discovery_has(bits,id));
    if(id<=5){cost[id]=1;seen[id]=1;n++;assert(!eg_catalogue_ingredients(id,&a,&b));}
    else {assert(eg_catalogue_ingredients(id,&a,&b));assert(eg_catalogue_recipe(a,b)==id);assert(eg_catalogue_recipe(b,a)==id);}
  }
  assert(eg_discovery_count(bits)==444);assert(bits[55]==15);bits[55]=255;assert(eg_discovery_count(bits)==444);
  assert(!eg_discovery_mark(bits,0) && !eg_discovery_mark(bits,445));assert(!eg_discovery_has(bits,0));
  // Enumerate the full symmetric pair space. Each pair has exactly one result; advanced forms also obey the
  // same family reaction and give alternative routes to a discovery.
  for(a=1;a<=444;a++)for(b=a;b<=444;b++) {
    r=eg_catalogue_recipe(a,b);assert(r==eg_catalogue_recipe(b,a));if(!r)continue;
    assert(r>5 && r<=444);incoming[r]++;recipes++;
  }
  assert(recipes==5283);for(id=6;id<=444;id++)assert(incoming[id]>=1);
  assert(eg_catalogue_recipe(eg_catalogue_variant(1,2),eg_catalogue_variant(2,2))==eg_catalogue_variant(13,2));
  assert(eg_catalogue_recipe(eg_catalogue_variant(1,12),eg_catalogue_variant(2,2))==eg_catalogue_variant(13,2));
  while(n<444 && passes++<444)for(id=6;id<=444;id++)if(!seen[id]) {
    assert(eg_catalogue_ingredients(id,&a,&b));if(!seen[a] || !seen[b])continue;
    cost[id]=(uint16_t)(cost[a]+cost[b]);depth[id]=(uint16_t)((depth[a]>depth[b] ? depth[a] : depth[b])+1);seen[id]=1;n++;
  }
  assert(n==444);for(id=6;id<=444;id++)assert(cost[id]>1 && cost[id]<=28);
  for(id=385;id<=444;id++)assert(cost[id]>=8 && depth[id]>=5);
  for(id=1;id<=24;id++)for(a=0;a<16;a++){r=eg_catalogue_variant((uint8_t)id,(uint8_t)a);assert(eg_catalogue_family(r)==id && eg_catalogue_form(r)==a);}
  assert(!eg_catalogue_recipe(0,1) && !eg_catalogue_recipe(445,1));
  {uint8_t counts[24]={0};
   counts[17]=1u;counts[1]=1u;assert(eg_catalogue_capabilities(counts)&CC_MUD); /* LIFE + WATER -> VINE */
   counts[3]=1u;assert(eg_catalogue_capabilities(counts)&CC_WEIGHT); /* VINE + EARTH -> WOOD */
   memset(counts,0,sizeof counts);counts[0]=1u;counts[7]=1u;assert(!(eg_catalogue_capabilities(counts)&CC_WEIGHT)); /* one brick route is not counted twice */
   memset(counts,0,sizeof counts);counts[23]=1u;assert(eg_catalogue_capabilities(counts)&CC_COLD);
  }
  printf("444 distinct reachable discoveries; 5283 symmetric recipes; base-only roots; max 28 base units; 56-byte collection.\n");return 0;
}
