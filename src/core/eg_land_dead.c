#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_land_dead.h"
#include "eg_piece.h"
enum { D_GOAL,D_ITEMS,D_ACTIVE,D_CACHE,D_CANDIDATE,D_DONE,D_LOST };
enum { S_MODE,S_GOAL,S_CHANNEL,S_CUT,S_NEED,S_POSSIBLE,S_ITEM,S_CURSOR,S_GROUP,S_BYTE,S_BIT,S_MASK,S_KIND,S_X,S_Y,S_CAND,
       C_FIRE,C_MUD,C_EARTH,C_WATER,C_BRICK,C_ENERGY,C_METAL,C_AIR,C_ELECTRIC,C_HOT,C_RAIN,C_SNOW,C_ICE,
       S_DATA,S_DATA1,S_DATA2,S_DATA3,S_BASEX,S_BASEY,S_UNUSED };
typedef char dead_state_layout[(S_UNUSED+1u==EL_DEAD_BYTES) ? 1 : -1];
static uint8_t minimum(uint8_t a,uint8_t b) {return a<b ? a : b;}
static void count(uint8_t *s,uint8_t piece) {
  uint8_t k=255u;
  if(piece==EG_ASSIST_COLD){s[S_UNUSED]|=1u;return;}
  if(piece==EG_ASSIST_POWER){s[S_UNUSED]|=2u;return;}
  if(piece==EG_ASSIST_MUD){s[S_UNUSED]|=4u;return;}
  if(EG_PIECE_HOT(piece) && s[C_HOT]<3u)s[C_HOT]++;
  switch(piece) {
    case P_FIRE:k=C_FIRE;break;case P_MUD:k=C_MUD;break;case P_EARTH:k=C_EARTH;break;
    case P_WATER:k=C_WATER;break;case P_BRICK:k=C_BRICK;break;case P_ENERGY:k=C_ENERGY;break;
    case P_METAL:k=C_METAL;break;case P_AIR:k=C_AIR;break;case P_ELECTRIC:k=C_ELECTRIC;break;
    case P_RAIN:k=C_RAIN;break;case P_SNOW:k=C_SNOW;break;case P_ICE:k=C_ICE;break;
    case EG_ASSIST_WEIGHT:k=C_BRICK;break;
  }
  if(k!=255u && s[k]<3u)s[k]++;
}
static uint8_t raw(const eg_land_dead_io *io,uint8_t x,uint8_t y,uint8_t initial) {
  return io->known(io->ctx,x,y) ? io->read(io->ctx,x,y) : initial;
}
static uint8_t weight(uint8_t t) {
  return t==T_BLOCK_PLATE || t==(T_PLATE|F_OB) || t==T_SOCKET_ON || (t&(T_MASK|F_PLATE))==(T_ROCK|F_PLATE);
}
static void advance(uint8_t *s,const eg_land *p) {
  uint8_t kind=p->node[s[S_GOAL]].kind;
  if(kind==EL_LOAD || kind==EL_POWER)s[S_CHANNEL]++;
  s[S_GOAL]++;s[S_MODE]=D_GOAL;
}
static uint8_t permanent(const eg_land *p,const eg_land_dead_io *io,int16_t x,int16_t y) {
  uint8_t t;
  if((uint16_t)x>=p->width || (uint16_t)y>=p->height)return 1u;
  if(!io->known(io->ctx,(uint8_t)x,(uint8_t)y))return 0u;
  t=io->read(io->ctx,(uint8_t)x,(uint8_t)y);
  return EG_ABYSS(t) || (t&T_MASK)==T_WALL;
}
static uint8_t candidate(const eg_land *p,const eg_land_node *n,const eg_land_dead_io *io,
                          uint8_t x,uint8_t y,uint8_t t) {
  uint8_t u=p->vertical ? y : x,v=p->vertical ? x : y;
  if(u>=n->at)return 0u;
  /* A weight holding the other half of an AND may not be counted twice. */
  if(n->condition==EL_BOTH && weight(t) &&
     ((u==n->at-3u && v==n->lane-1u) || (u==n->at-4u && v==n->lane+2u)))return 0u;
  if((t&T_MASK)==T_ROCK)return 1u; /* gravity/rolls stay optimistic */
  if(t&F_OB)return 1u; /* living weights keep the certificate optimistic */
  if(!EG_CRATE(t))return 0u;
  /* This codec has an unbuildable exterior rim and interior apparatus/portal
   * endpoints. A crate parked on the inner global rim cannot be pushed inward. */
  if(x<=1u || y<=1u || x>=p->width-2u || y>=p->height-2u)return 0u;
  return !((permanent(p,io,(int16_t)x-1,y) || permanent(p,io,(int16_t)x+1,y)) &&
           (permanent(p,io,x,(int16_t)y-1) || permanent(p,io,x,(int16_t)y+1)));
}
void eg_land_dead_begin(eg_land_dead *state) EG_BANKED {memset(state,0,sizeof *state);}
uint8_t eg_land_dead_step(eg_land_dead *state,const eg_land *p,const eg_view *view,const eg_room *r,
                          uint8_t from,const uint8_t *bag,uint8_t nbag,uint8_t latched,
                          const eg_land_dead_io *io,uint8_t budget) EG_BANKED {
  uint8_t *s=state->b,k,x,y,a,b,t,crafts,need;const eg_land_node *n;
  if(!budget)return EG_DEAD_BUSY;
  while(budget--) {
    if(s[S_MODE]==D_DONE)return 0u;
    if(s[S_MODE]==D_LOST)return s[S_KIND]==EL_LOAD ? EG_DEAD_DOOR : EG_DEAD_ITEM;
    if(s[S_GOAL]>=p->nnode) {s[S_MODE]=D_DONE;return 0u;}
    n=&p->node[s[S_GOAL]];
    if(s[S_MODE]==D_GOAL) {
      s[S_KIND]=n->kind;s[S_CUT]=n->at;
      if((from<EG_N && (p->vertical ? view->y+EG_Y(from) : view->x+EG_X(from))>n->at) ||
         n->kind==EL_PORTAL || (n->kind==EL_COLD && (n->alternatives&EL_JUMP)) ||
         (n->kind==EL_LOAD && (n->condition==EL_LATCH || (latched&(1u<<s[S_CHANNEL]))))) {advance(s,p);continue;}
      x=EL_X(p,n->at-3u,n->lane-1u);y=EL_Y(p,n->at-3u,n->lane-1u);
      if(n->kind==EL_LOAD) {
        a=weight(raw(io,x,y,T_PLATE));b=0u;
        if(n->condition>=EL_EITHER)b=weight(raw(io,EL_X(p,n->at-4u,n->lane+2u),EL_Y(p,n->at-4u,n->lane+2u),T_PLATE));
        need=n->condition==EL_EITHER ? (uint8_t)(!(a||b)) : n->condition==EL_BOTH ? (uint8_t)(2u-a-b) : (uint8_t)!a;
        if(!need) {advance(s,p);continue;}s[S_NEED]=need;
      } else if(n->kind==EL_POWER) {
        if(raw(io,x,y,T_SOCKET)==T_SOCKET_ON) {advance(s,p);continue;}
      } else {
        t=raw(io,EL_X(p,n->at,n->lane),EL_Y(p,n->at,n->lane),n->kind==EL_HEAT ? T_PAPER : T_WATER);
        if(t!=(n->kind==EL_HEAT ? T_PAPER : T_WATER)) {advance(s,p);continue;}
      }
      for(k=C_FIRE;k<=C_ICE;k++)s[k]=0u;
      s[S_UNUSED]=0u;
      for(k=0u;k<nbag;k++)count(s,bag[k]);s[S_ITEM]=0u;s[S_MODE]=D_ITEMS;continue;
    }
    if(s[S_MODE]==D_ITEMS) {
      if(s[S_ITEM]<p->nitem) {
        const eg_land_item *it=&p->item[s[S_ITEM]++];
        if(it->u<n->at && (raw(io,EL_X(p,it->u,it->v),EL_Y(p,it->u,it->v),T_ITEM)&T_MASK)==T_ITEM)count(s,it->piece);
        continue;
      }
      if(n->kind==EL_HEAT) {if(s[C_HOT])advance(s,p);else s[S_MODE]=D_LOST;continue;}
      if(n->kind==EL_POWER) {
        if((s[S_UNUSED]&2u) || s[C_ELECTRIC] || (s[C_METAL] && (s[C_ENERGY] || (s[C_FIRE] && s[C_AIR]))))advance(s,p);else s[S_MODE]=D_LOST;
        continue;
      }
      if(n->kind==EL_COLD) {
        if((s[S_UNUSED]&1u) || s[C_SNOW] || s[C_ICE] || (s[C_AIR] && (s[C_RAIN] || (s[C_AIR]>=2u && s[C_WATER]))))advance(s,p);else s[S_MODE]=D_LOST;
        continue;
      }
      crafts=(uint8_t)(s[C_BRICK]+minimum(s[C_FIRE],(uint8_t)(s[C_MUD]+minimum(s[C_EARTH],s[C_WATER]))));
      if(crafts>=s[S_NEED]) {advance(s,p);continue;}
      s[S_NEED]=(uint8_t)(s[S_NEED]-crafts);s[S_POSSIBLE]=0u;
      /* Untouched canonical crates still exist. Changed chunks are checked
       * from their real active/backing material, including crafted weights. */
      for(k=0u;k<p->nnode;k++)if(p->node[k].kind==EL_LOAD && p->node[k].at-3u<n->at) {
        const eg_land_node *other=&p->node[k];
        x=EL_X(p,other->at-3u,other->lane+1u);y=EL_Y(p,other->at-3u,other->lane+1u);
        if(!io->known(io->ctx,x,y))s[S_POSSIBLE]++;
        if(other->condition>=EL_EITHER && !io->known(io->ctx,EL_X(p,other->at-3u,other->lane+2u),EL_Y(p,other->at-3u,other->lane+2u)))s[S_POSSIBLE]++;
      }
      if(s[S_POSSIBLE]>=s[S_NEED]) {advance(s,p);continue;}
      s[S_CURSOR]=0u;s[S_MODE]=D_ACTIVE;continue;
    }
    if(s[S_MODE]==D_ACTIVE) {
      for(k=0u;k<4u && s[S_CURSOR]<EG_N;k++) {
        t=s[S_CURSOR]++;a=r->c[t];
        if(EG_CRATE(a) || (a&T_MASK)==T_ROCK || (a&F_OB)) {
          x=(uint8_t)(view->x+EG_X(t));y=(uint8_t)(view->y+EG_Y(t));
          if(candidate(p,n,io,x,y,a))s[S_POSSIBLE]++;
        }
        if(s[S_POSSIBLE]>=s[S_NEED])break;
      }
      if(s[S_POSSIBLE]>=s[S_NEED]) {advance(s,p);continue;}
      if(s[S_CURSOR]==EG_N) {s[S_MODE]=D_CACHE;s[S_GROUP]=s[S_BYTE]=s[S_MASK]=0u;}
      continue;
    }
    if(s[S_MODE]==D_CACHE) {
      if(!s[S_MASK]) {
        if(s[S_GROUP]>=27u) {s[S_MODE]=D_LOST;continue;}
        s[S_MASK]=io->mask(io->ctx,s[S_GROUP],s[S_BYTE]);s[S_BIT]=0u;
        s[S_BASEX]=(uint8_t)((s[S_BYTE]&3u)<<5);s[S_BASEY]=(uint8_t)(s[S_GROUP]*2u+(s[S_BYTE]>>2));
        if(++s[S_BYTE]==8u) {s[S_BYTE]=0u;s[S_GROUP]++;}
        if(!s[S_MASK])continue;
      }
      while(!(s[S_MASK]&1u)) {s[S_MASK]>>=1;s[S_BIT]++;}
      x=(uint8_t)(s[S_BASEX]+s[S_BIT]*4u);y=s[S_BASEY];s[S_MASK]>>=1;s[S_BIT]++;
      if(x>=p->width || y>=p->height || (p->vertical ? y : x)>=n->at ||
         (x>=view->x && x<view->x+16u && y>=view->y && y<view->y+14u))continue;
      io->quad(io->ctx,x,y,s+S_DATA);s[S_X]=x;s[S_Y]=y;s[S_CAND]=0u;s[S_MODE]=D_CANDIDATE;continue;
    }
    if(s[S_MODE]==D_CANDIDATE) {
      while(s[S_CAND]<4u) {
        k=s[S_CAND]++;t=s[S_DATA+k];
        if(EG_CRATE(t) || (t&T_MASK)==T_ROCK || (t&F_OB)) {
          if(candidate(p,n,io,(uint8_t)(s[S_X]+k),s[S_Y],t))s[S_POSSIBLE]++;
          break;
        }
      }
      if(s[S_POSSIBLE]>=s[S_NEED]) {advance(s,p);continue;}
      if(s[S_CAND]==4u)s[S_MODE]=D_CACHE;
    }
  }
  return EG_DEAD_BUSY;
}
