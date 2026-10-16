#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_room.h"
#include "../src/core/eg_physics.h"
#include "../src/core/eg_stage.h"
#include "../src/core/eg_piece.h"
static unsigned checked;
static void check(const eg_room *r) {
  uint8_t x,y;
  assert(r->c[r->start]!=T_PIT && r->c[r->door]==T_DOOR);
  for(y=0u;y!=EG_H;y++) for(x=0u;x!=EG_W;x++)
    if(!x || x==EG_W-1u || y==EG_H-1u) {
      /* Sockets and other actual architecture may occupy a rim; only the
       * formerly invisible concrete boundary must disappear. */
      assert(r->c[EG_IDX(x,y)]!=T_WALL);
    }
  checked++;
}
int main(void) {
  eg_room r,before;eg_stage s;unsigned seed,tier,type;
  memset(&r,0,sizeof r);
  for(seed=0;seed<EG_N;seed++) r.c[seed]=T_WALL;
  before=r;eg_island_edges(&r);
  for(seed=0;seed<EG_N;seed++)
    if(EG_X(seed) && EG_X(seed)<EG_W-1u && EG_Y(seed)<EG_H-1u) assert(r.c[seed]==before.c[seed]);
  before=r;eg_island_edges(&r);assert(!memcmp(&before,&r,sizeof r));
  {
    uint8_t a=EG_IDX(1u,8u),b=EG_IDX(0u,8u),p;
    memset(&r,0,sizeof r);r.c[b]=T_PIT|F_ABYSS;r.c[a]=T_BLOCK;
    assert(eg_push_block(&r,a,b) && !r.c[a] && EG_ABYSS(r.c[b]));
    r.c[a]=T_ROCK;assert(eg_push(&r,a,b) && !r.c[a] && EG_ABYSS(r.c[b]));
    for(p=1u;p<P_COUNT;p++) assert(!eg_apply(&r,p,b,0,0u) && EG_ABYSS(r.c[b]));
    a=EG_IDX(4u,12u);b=EG_IDX(4u,13u);r.c[b]=T_PIT|F_ABYSS;r.c[a]=T_ROCK|F_FALL;
    assert((eg_gravity(&r,&b,1u)&EG_G_HIT) && (r.c[a]&T_MASK)==T_ROCK);
    eg_gravity(&r,0,0u);assert(!r.c[a] && EG_ABYSS(r.c[b]));
    b=EG_IDX(4u,8u);r.c[b]=T_PIT;
    assert(eg_apply(&r,P_MUD,b,0,0u)==RX_MUD && r.c[b]!=T_PIT);
    a=EG_IDX(4u,0u);r.c[a]=T_BLOCK;before=r;
    assert(!eg_push_block(&r,a,(uint8_t)(a-EG_W)) && !memcmp(&r,&before,sizeof r));
    a=EG_IDX(15u,4u);r.c[a]=T_BLOCK;before=r;
    assert(!eg_push_block(&r,a,(uint8_t)(a+1u)) && !memcmp(&r,&before,sizeof r));
    r.c[a]=T_ROCK;before=r;
    assert(!eg_push(&r,a,(uint8_t)(a+1u)) && !memcmp(&r,&before,sizeof r));
    assert(!eg_push(&r,240u,224u) && !eg_push_block(&r,240u,224u));
  }
  for(seed=1u;seed<=64u;seed++) {
    for(tier=0u;tier<6u;tier++) if(eg_generate(&r,(uint16_t)(seed*7919u),(uint8_t)tier)) check(&r);
    for(type=PH_PUSH;type<PH_COUNT;type++) {eg_physics_gen(&r,(uint8_t)type,(uint16_t)(seed*7919u),8u);check(&r);}
    for(type=1u;type<EG_DECK_TYPES;type++)
      if(eg_stage_gen(&r,&s,(uint8_t)type,(uint16_t)(seed*7919u),(uint8_t)(seed&15u),255u)) check(&r);
  }
  printf("island edges: %u physical, reaction, obstacle and combat rooms; north/interior preservation and idempotence pass\n",checked);
  return 0;
}
