/* Pin seeded benches while preserving every other accepted-room byte and RNG. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_plan.h"
static uint64_t digest=UINT64_C(14695981039346656037);
static uint64_t baseline=UINT64_C(14695981039346656037);
static void hash(uint64_t *state,const void *p,size_t n) {
  const uint8_t *b=p;while(n--) {*state^=*b++;*state*=UINT64_C(1099511628211);}
}
int main(void) {
  eg_room room;eg_seed_rec rec;uint8_t ok;unsigned k,accepted=0u;
  assert(offsetof(eg_room,run)+EG_MAX_RUN==308u);
  for(k=0u;k<EG_SEEDS+4096u;k++) {
    memset(&room,0,sizeof room);
    if(k<EG_SEEDS)eg_seed_get((uint16_t)k,&rec);
    else {rec.seed=(uint16_t)((k-EG_SEEDS)*7919u+1u);rec.tier=(uint8_t)((k-EG_SEEDS)%6u);}
    ok=eg_generate(&room,rec.seed,rec.tier);if(k<EG_SEEDS)assert(ok);
    accepted+=ok;hash(&digest,&ok,1u);hash(&digest,&room,308u);hash(&digest,&eg_rs,sizeof eg_rs);
    /* A bench replaces an existing wall before island_edges opens its rim.
     * Normalize that one intentional delta to retain the prior geometry,
     * resources, obstacle data, acceptance decisions and RNG contract. */
    for(unsigned i=0u;i<EG_N;i++)if(EG_BENCH(room.c[i]))
      room.c[i]=!EG_X(i) || EG_X(i)==15u || EG_Y(i)==13u ? T_PIT|F_ABYSS : T_WALL;
    hash(&baseline,&ok,1u);hash(&baseline,&room,308u);hash(&baseline,&eg_rs,sizeof eg_rs);
  }
  assert(baseline==UINT64_C(0x0c29c3fce3a2a2f2));
  assert(digest==UINT64_C(0xda18ae1385267dab));
  printf("generator fingerprint: %u cases (%u constructed), %016llx; baseline %016llx\n",k,accepted,(unsigned long long)digest,(unsigned long long)baseline);
  return 0;
}
