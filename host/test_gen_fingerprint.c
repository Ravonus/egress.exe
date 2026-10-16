/* Preserve accepted rooms and generation RNG while compacting construction scratch. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_plan.h"
static uint64_t digest=UINT64_C(14695981039346656037);
static void hash(const void *p,size_t n) {
  const uint8_t *b=p;while(n--) {digest^=*b++;digest*=UINT64_C(1099511628211);}
}
int main(void) {
  eg_room room;eg_seed_rec rec;uint8_t ok;unsigned k,accepted=0u;
  assert(offsetof(eg_room,run)+EG_MAX_RUN==308u);
  for(k=0u;k<EG_SEEDS+4096u;k++) {
    memset(&room,0,sizeof room);
    if(k<EG_SEEDS)eg_seed_get((uint16_t)k,&rec);
    else {rec.seed=(uint16_t)((k-EG_SEEDS)*7919u+1u);rec.tier=(uint8_t)((k-EG_SEEDS)%6u);}
    ok=eg_generate(&room,rec.seed,rec.tier);if(k<EG_SEEDS)assert(ok);
    accepted+=ok;hash(&ok,1u);hash(&room,308u);hash(&eg_rs,sizeof eg_rs);
  }
  assert(digest==UINT64_C(0x0c29c3fce3a2a2f2));
  printf("generator fingerprint: %u cases (%u constructed), %016llx\n",k,accepted,(unsigned long long)digest);
  return 0;
}
