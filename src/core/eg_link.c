/* Named pressure/socket circuits and entry-triggered portals. Portable rules;
 * no cache, presentation state or global RNG participates in the result. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_room.h"

static uint8_t occupied(uint8_t c,const uint8_t *occ,uint8_t n) {
  while(n) if(occ[--n]==c) return 1u;
  return 0u;
}
static void mark(uint8_t c) {
  if(eg_ndirty<EG_DIRTY_MAX) eg_dirty[eg_ndirty]=c;
  if(eg_ndirty!=255u) eg_ndirty++;
}
void eg_links_clear(eg_room *r) EG_BANKED {
  uint8_t i,*p=&r->nlink;
  for(i=0u;i!=12u;i++) p[i]=0u;
}
static uint8_t source(const eg_room *r,uint8_t c,const uint8_t *occ,uint8_t n,uint8_t external) {
  uint8_t v;
  if(c==EG_LINK_EXTERNAL) return (uint8_t)(external!=0u);
  if(c>=EG_N) return 0u;
  v=r->c[c];
  return (uint8_t)(v==T_SOCKET_ON || v==T_BLOCK_PLATE || v==(T_PLATE|F_OB) ||
    (v&(T_MASK|F_PLATE))==(T_ROCK|F_PLATE) || (v==T_PLATE && occupied(c,occ,n)));
}
static uint8_t power(const eg_room *r,uint8_t k,const uint8_t *occ,uint8_t n) {
  const eg_link *l=&r->link[k];uint8_t a,b;
  if(l->rule&EG_LINK_LATCH) {
    if(l->gate<EG_N && (r->c[l->gate]&T_MASK)==T_GATE_OPEN) return 1u;
    if(l->gate==EG_LINK_EXTERNAL && (l->rule&EG_LINK_EXT_LATCH)) return 1u;
  }
  a=source(r,l->a,occ,n,(uint8_t)(l->rule&EG_LINK_EXT_A));
  if(l->b==EG_LINK_NONE) return a;
  b=source(r,l->b,occ,n,(uint8_t)(l->rule&EG_LINK_EXT_B));
  return (uint8_t)(l->rule&EG_LINK_ANY ? (a|b) : (a&b));
}
uint8_t eg_link_power(const eg_room *r,uint8_t k,const uint8_t *occ,uint8_t n) EG_BANKED {
  if(k>=r->nlink || k>=EG_LINKS) return 0u;
  return power(r,k,occ,n);
}
uint8_t eg_circuits(eg_room *r,const uint8_t *occ,uint8_t n) EG_BANKED {
  uint8_t k,g,v,w,res=0u;
  for(k=0u;k<r->nlink && k<EG_LINKS;k++) {
    g=r->link[k].gate;if(g>=EG_N) continue;
    v=r->c[g];if((v&T_MASK)!=T_GATE && (v&T_MASK)!=T_GATE_OPEN) continue;
    w=power(r,k,occ,n);
    if(w && (v&T_MASK)==T_GATE) {
      r->c[g]=(uint8_t)((v&F_OB)|T_GATE_OPEN);mark(g);res|=EG_G_CHANGED;
    } else if(!w && (v&T_MASK)==T_GATE_OPEN) {
      if((v&F_OB) || occupied(g,occ,n)) {eg_plates_dirty=1u;continue;}
      r->c[g]=T_GATE;mark(g);res|=EG_G_CHANGED;
    }
  }
  return res;
}
static uint8_t target(const eg_room *r,uint8_t from,const uint8_t *occ,uint8_t n) {
  uint8_t to,k,need=(uint8_t)(r->portal_need&3u);
  if(!(r->portal_need&EG_PORTAL_ON) || from>=EG_N) return EG_LINK_NONE;
  if(from==r->portal[0]) to=r->portal[1];
  else if(from==r->portal[1]) to=r->portal[0];
  else return EG_LINK_NONE;
  if(to>=EG_N || to==from || occupied(to,occ,n)) return EG_LINK_NONE;
  for(k=0u;k!=EG_LINKS;k++) if((need&(1u<<k)) && (k>=r->nlink || !power(r,k,occ,n))) return EG_LINK_NONE;
  return to;
}
uint8_t eg_portal_target(const eg_room *r,uint8_t from,const uint8_t *occ,uint8_t n) EG_BANKED {
  uint8_t to=target(r,from,occ,n);
  if(to==EG_LINK_NONE || !EG_PASSABLE(r->c[to])) return EG_LINK_NONE;
  return to;
}
uint8_t eg_portal_object(eg_room *r,uint8_t from,const uint8_t *occ,uint8_t n) EG_BANKED {
  uint8_t to,v,t;
  if(from>=EG_N) return EG_LINK_NONE;
  v=r->c[from];if((v&T_MASK)!=T_ROCK && !EG_CRATE(v)) return EG_LINK_NONE;
  to=target(r,from,occ,n);
  if(to==EG_LINK_NONE || !EG_ROCK_OPEN(r->c[to])) return EG_LINK_NONE;
  t=r->c[to];
  if(EG_CRATE(v)) {
    r->c[from]=v==T_BLOCK_PLATE ? T_PLATE : T_EMPTY;
    r->c[to]=t==T_PLATE ? T_BLOCK_PLATE : T_BLOCK;
  } else {
    r->c[from]=v&F_PLATE ? T_PLATE : T_EMPTY;
    r->c[to]=(uint8_t)((v&(T_MASK|F_FALL|F_MOVED))|(t==T_PLATE ? F_PLATE : 0u));
  }
  eg_plates_dirty=1u;mark(from);mark(to);return to;
}
