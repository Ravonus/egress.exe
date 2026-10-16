/* Same full-run constructor, one native scratch slot, exact legacy layout digest. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_plan.h"
static uint64_t digest=UINT64_C(14695981039346656037);
static void hash(const void *p,size_t n) {const uint8_t *b=p;while(n--) {digest^=*b++;digest*=UINT64_C(1099511628211);}}
#ifndef EG_REFERENCE
static uint8_t count;
static void take(uint8_t i,const eg_slot *s,void *context) {
  eg_run *r=context;assert(i==count++ && i<EG_RUN_SLOTS);r->s[i]=*s;
}
#endif
int main(void) {
  eg_run_in in;eg_run expected;
#ifndef EG_REFERENCE
  eg_run got;
  struct {uint8_t before[32];eg_world world;eg_slot scratch;uint8_t after[32];} small;
  unsigned j;
  assert(sizeof(eg_world)+sizeof(eg_slot)<=64u);
  assert(offsetof(eg_run,s)==sizeof(eg_world));
  memset(&in,0,sizeof in);in.door=BI_COUNT;
  assert(!eg_run_stream(&small.world,&small.scratch,&in,take,&got));
  in.door=0u;in.mode=2u;
  assert(!eg_run_stream(&small.world,&small.scratch,&in,take,&got));
  assert(!eg_run_stream(0,&small.scratch,&in,take,&got));
#endif
  for(unsigned k=0;k<2048u;k++) {
    memset(&in,0,sizeof in);in.run_seed=(uint16_t)(k*7919u+1u);in.cycle_seed=(uint16_t)(k*40503u+17u);
    in.generation=(uint8_t)(k%64u);in.lineage=(uint8_t)(k%4u);in.cycle=(uint8_t)(1u+k%5u);
    in.door=(uint8_t)(k%7u);in.mode=(uint8_t)((k%5u)==0u);in.stratum=(uint8_t)(k%24u);in.sky=(uint8_t)(k&1u);
    in.seen=(uint32_t)(k*UINT32_C(2654435761))&((UINT32_C(1)<<MK_ALL)-1u);
    in.developed=in.seen&(uint32_t)(k*UINT32_C(40503));
    if(k%8u==0u)in.seen=in.developed=0u;
    memset(&expected,0,sizeof expected);eg_run_build(&expected,&in);
    assert(expected.n && expected.n<=EG_RUN_SLOTS);
    hash(&expected,offsetof(eg_run,s));hash(expected.s,(size_t)expected.n*sizeof(eg_slot));
#ifndef EG_REFERENCE
    memset(&small,0xA7,sizeof small);memset(&got,0,sizeof got);count=0u;
    assert(eg_run_stream(&small.world,&small.scratch,&in,take,&got)==expected.n);
    assert(count==expected.n);assert(!memcmp(&small.world,&expected,sizeof small.world));
    assert(!memcmp(got.s,expected.s,(size_t)expected.n*sizeof(eg_slot)));
    for(j=0u;j<32u;j++)assert(small.before[j]==0xA7u && small.after[j]==0xA7u);
#endif
  }
  assert(digest==UINT64_C(0x36f811762b4adee7));
  printf("run stream: 2048 exact layouts, digest %016llx\n",(unsigned long long)digest);
  return 0;
}
