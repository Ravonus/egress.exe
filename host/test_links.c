#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/eg_room.h"
#include "../src/core/eg_piece.h"

static eg_room r;
static const uint8_t a=114u,b=116u,g=88u,h=92u,p=147u,q=71u;
static void base(void) {
  memset(&r,0,sizeof r);r.nlink=2u;
  r.link[0]=(eg_link){a,b,g,0u};r.link[1]=(eg_link){130u,EG_LINK_NONE,h,0u};
  r.c[a]=r.c[b]=r.c[130u]=T_PLATE;r.c[g]=r.c[h]=T_GATE;
  r.portal[0]=p;r.portal[1]=q;r.portal_need=EG_PORTAL_ON|1u;
}
int main(void) {
  uint8_t mask,rule,occ[2],to,flags;
  for(mask=1u;mask<P_COUNT;mask++)assert(!!EG_PIECE_HOT(mask)==!!(PC_TAGS(mask)&TG(TAG_HOT)));
  for(mask=0u;mask<EG_RECIPES;mask++)if(EG_PIECE_HOT(eg_recipes[mask][2]))
    assert(EG_PIECE_HOT(eg_recipes[mask][0]) || EG_PIECE_HOT(eg_recipes[mask][1]));
  for(rule=0u;rule!=2u;rule++) for(mask=0u;mask!=4u;mask++) {
    base();r.link[0].rule=rule;
    if(mask&1u)r.c[a]=T_BLOCK_PLATE;
    if(mask&2u)r.c[b]=T_ROCK|F_PLATE;
    eg_shutters(&r,0,0u);
    assert((r.c[g]==T_GATE_OPEN)==(rule ? mask!=0u : mask==3u));
    assert(r.c[h]==T_GATE);
  }
  base();r.link[0].b=EG_LINK_NONE;occ[0]=a;
  eg_shutters(&r,occ,1u);assert(r.c[g]==T_GATE_OPEN);
  occ[0]=g;eg_shutters(&r,occ,1u);assert(r.c[g]==T_GATE_OPEN);
  /* A safety-held gate must not masquerade as a powered circuit. */
  assert(!eg_link_power(&r,0u,occ,1u));assert(eg_portal_target(&r,p,occ,1u)==EG_LINK_NONE);
  eg_shutters(&r,0,0u);assert(r.c[g]==T_GATE);
  r.link[0].rule=EG_LINK_LATCH;occ[0]=a;
  eg_shutters(&r,occ,1u);eg_shutters(&r,0,0u);assert(r.c[g]==T_GATE_OPEN);
  assert(eg_link_power(&r,0u,0,0u));
  base();r.c[a]=T_SOCKET_ON;r.c[b]=T_PLATE|F_OB;
  assert(eg_link_power(&r,0u,0,0u));
  r.link[1].a=EG_LINK_EXTERNAL;r.link[1].rule=EG_LINK_EXT_A;
  eg_shutters(&r,0,0u);assert(r.c[g]==T_GATE_OPEN && r.c[h]==T_GATE_OPEN);
  r.portal_need|=2u;r.link[1].rule=0u;
  assert(eg_portal_target(&r,p,0,0u)==EG_LINK_NONE);
  r.link[1].rule=EG_LINK_EXT_A;
  assert(eg_portal_target(&r,p,0,0u)==q);
  occ[0]=q;assert(eg_portal_target(&r,p,occ,1u)==EG_LINK_NONE);
  r.c[q]=T_EMPTY|F_OB;assert(eg_portal_target(&r,p,0,0u)==EG_LINK_NONE);
  r.c[q]=T_EMPTY;
  for(flags=0u;flags!=4u;flags++) {
    r.c[p]=(uint8_t)(T_ROCK|F_PLATE|(flags&1u ? F_FALL : 0u)|(flags&2u ? F_MOVED : 0u));
    r.c[q]=T_PLATE;eg_ndirty=0u;
    to=eg_portal_object(&r,p,0,0u);assert(to==q && r.c[p]==T_PLATE);
    assert(r.c[q]==(uint8_t)(T_ROCK|F_PLATE|(flags&1u ? F_FALL : 0u)|(flags&2u ? F_MOVED : 0u)));
    assert(eg_ndirty==2u && eg_dirty[0]==p && eg_dirty[1]==q);
  }
  r.c[p]=T_BLOCK_PLATE;r.c[q]=T_EMPTY;
  assert(eg_portal_object(&r,p,0,0u)==q && r.c[p]==T_PLATE && r.c[q]==T_BLOCK);
  r.c[p]=T_BLOCK;r.c[q]=T_WALL;
  assert(eg_portal_object(&r,p,0,0u)==EG_LINK_NONE && r.c[p]==T_BLOCK);
  /* Falling through an upward portal cannot run the same CRT a second time. */
  base();r.portal_need=EG_PORTAL_ON;r.portal[0]=146u;r.portal[1]=66u;
  r.c[130u]=T_ROCK;r.c[66u]=T_EMPTY;r.c[82u]=T_EMPTY;
  eg_gravity(&r,0,0u);assert((r.c[66u]&T_MASK)==T_ROCK && r.c[82u]==T_EMPTY);
  eg_gravity(&r,0,0u);assert(r.c[66u]==T_EMPTY && (r.c[82u]&T_MASK)==T_ROCK);
  /* Wind shares the exact finite-use object path, with occupied exits refused. */
  base();r.portal_need=EG_PORTAL_ON;r.portal[0]=148u;r.c[147u]=T_BLOCK;occ[0]=146u;
  assert(eg_apply(&r,P_AIR,147u,occ,1u)==RX_PUSH && r.c[q]==T_BLOCK && r.c[148u]==T_EMPTY);
  eg_links_clear(&r);assert(r.nlink==0u && r.portal_need==0u);
  puts("circuits: independent AND/OR/latch, sockets/weights, gate safety, conditional portals, push/fall/wind and occupancy pass");
  return 0;
}
