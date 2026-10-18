#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "eg_bag.h"
int main(void) {
 uint8_t lo[24]={0},hi[3]={0},n=0,k,slot;eg_item_id values[24]={1,255,256,257,444,16,384,385,408,409,432,433,24,25,100,300,2,3,4,5,6,7,8,9};
 for(k=0;k<24;k++){assert(eg_bag_put(lo,hi,k,values[k]));n++;assert(eg_bag_valid(lo,hi,n));}
 for(k=0;k<24;k++)assert(eg_bag_get(lo,hi,k)==values[k]);
 assert(!eg_bag_put(lo,hi,24,1) && !eg_bag_put(lo,hi,0,445));assert(eg_bag_get(lo,hi,4)==444);
 for(slot=0;slot<24;slot++) {
  uint8_t copy[24],high[3],count=24;memcpy(copy,lo,24);memcpy(high,hi,3);
  assert(eg_bag_remove(copy,high,&count,slot));assert(count==23 && eg_bag_valid(copy,high,count));
  for(k=0;k<count;k++)assert(eg_bag_get(copy,high,k)==values[k+(k>=slot)]);
 }
 while(n)assert(eg_bag_remove(lo,hi,&n,(uint8_t)(n/2)));
 assert(eg_bag_valid(lo,hi,0));for(k=0;k<24;k++)assert(lo[k]==0);for(k=0;k<3;k++)assert(hi[k]==0);
 assert(!eg_bag_remove(lo,hi,&n,0));assert(!eg_bag_valid(lo,hi,25));
 assert(eg_bag_put(lo,hi,0,444));assert(!eg_bag_valid(lo,hi,0));assert(eg_bag_valid(lo,hi,1));hi[0]|=2;assert(!eg_bag_valid(lo,hi,1));
 puts("Nine-bit inventory crosses 255/256, round-trips 444, compacts every slot and rejects invalid checkpoints.");return 0;
}
