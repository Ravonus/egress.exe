/* Global, finite-resource witness replay. Every mutation is made by the same
 * 16x14 push/circuit/recipe core used by the cartridge. Global reachability is
 * rechecked between actions; reserved floor alone is not a solution proof. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/eg_land.h"
#include "core/eg_piece.h"
static eg_land p;
#undef assert
#define assert(c) do {if(!(c)) {fprintf(stderr,"land assertion %s:%u: %s (seed %u rule %u size %u biome %u %ux%u vertical %u)\n",__FILE__,__LINE__,#c,p.seed,p.rule,p.size,p.biome,p.width,p.height,p.vertical);exit(1);}}while(0)
static uint8_t board[128][128],seen[128][128],bag[P_COUNT];
static uint16_t queue[16384];
static uint8_t ou,ov;
static unsigned replays,pushes,crafts,portals;
static uint8_t read_at(uint8_t u,uint8_t v) {return board[u][v];}
static uint8_t local(uint8_t u,uint8_t v) {
  return u>=ou && u<ou+16u && v>=ov && v<ov+14u ? EG_IDX(u-ou,v-ov) : EG_LINK_NONE;
}
static uint8_t source(uint8_t u,uint8_t v,uint8_t *rule,uint8_t bit) {
  uint8_t c=local(u,v);
  if(c==EG_LINK_NONE) {
    c=EG_LINK_EXTERNAL;
    if(read_at(u,v)==T_BLOCK_PLATE || read_at(u,v)==T_SOCKET_ON)*rule|=bit;
  }
  return c;
}
static void extract(eg_room *r,const eg_land_node *n) {
  memset(r,0,sizeof *r);ou=n->at>=7u ? n->at-7u : 0u;ov=n->lane>=5u ? n->lane-5u : 0u;
  if(ou>EL_LENGTH(&p)-16u)ou=EL_LENGTH(&p)-16u;
  if(ov>EL_CROSS(&p)-14u)ov=EL_CROSS(&p)-14u;
  for(unsigned v=0;v<14;v++)for(unsigned u=0;u<16;u++)r->c[EG_IDX(u,v)]=board[ou+u][ov+v];
  for(unsigned k=0;k<p.nnode;k++) {
    const eg_land_node *g=&p.node[k];uint8_t i;
    if(g->kind==EL_LOAD || g->kind==EL_POWER) {
      assert(r->nlink<EG_LINKS);
      eg_link *l=&r->link[r->nlink++];
      l->rule=g->condition==EL_LATCH ? EG_LINK_LATCH : g->condition==EL_EITHER ? EG_LINK_ANY : 0;
      l->gate=local(g->at,g->lane);
      if(l->gate==EG_LINK_NONE && board[g->at][g->lane]==T_GATE_OPEN)l->rule|=EG_LINK_EXT_LATCH;
      l->a=source(g->at-3u,g->lane-1u,&l->rule,EG_LINK_EXT_A);l->b=EG_LINK_NONE;
      if(g->kind==EL_LOAD && g->condition>=EL_EITHER)l->b=source(g->at-4u,g->lane+2u,&l->rule,EG_LINK_EXT_B);
    } else if(g->kind==EL_PORTAL) {
      i=local(g->at-2u,g->lane);r->portal[0]=i;r->portal[1]=local(g->at+2u,g->lane);
      if(i<EG_N && r->portal[1]<EG_N)r->portal_need=EG_PORTAL_ON|1u;
    }
  }
}
static void apply_room(const eg_room *r) {
  for(unsigned v=0;v<14;v++)for(unsigned u=0;u<16;u++)board[ou+u][ov+v]=r->c[EG_IDX(u,v)];
}
static uint8_t walk(uint8_t t) {return EG_PASSABLE(t&T_MASK) || (t&T_MASK)==T_DOOR;}
static void reach(void) {
  unsigned head=0,tail=0;uint8_t u=2u,v=eg_land_lane(&p,u);
  memset(seen,0,sizeof seen);assert(walk(board[u][v]));seen[u][v]=1;queue[tail++]=(uint16_t)((u<<8)|v);
  while(head<tail) {
    uint16_t c=queue[head++];u=c>>8;v=c&255;
    for(unsigned d=0;d<4;d++) {
      int x=(int)u+(d==0 ? -1 : d==1 ? 1 : 0),y=(int)v+(d==2 ? -1 : d==3 ? 1 : 0);
      if(x<0 || y<0 || x>=EL_LENGTH(&p) || y>=EL_CROSS(&p) || seen[x][y] || !walk(board[x][y]))continue;
      seen[x][y]=1;queue[tail++]=(uint16_t)((x<<8)|y);
    }
    for(unsigned k=0;k<p.nnode;k++)if(p.node[k].kind==EL_PORTAL) {
      const eg_land_node *g=&p.node[k];uint8_t to=0;
      if(v!=g->lane || (u!=g->at-2u && u!=g->at+2u))continue;
      eg_room r;extract(&r,g);to=eg_portal_target(&r,local(u,v),0,0);
      if(to<EG_N) {
        uint8_t x=ou+EG_X(to),y=ov+EG_Y(to);
        if(!seen[x][y]) {seen[x][y]=1;queue[tail++]=(uint16_t)((x<<8)|y);}
      }
    }
  }
}
static void gather(void) {
  reach();
  for(unsigned k=0;k<p.nitem;k++) {
    const eg_land_item *it=&p.item[k];
    if(seen[it->u][it->v] && board[it->u][it->v]==T_ITEM) {bag[it->piece]++;board[it->u][it->v]=T_EMPTY;}
  }
}
static uint8_t make(uint8_t a,uint8_t b) {
  assert(bag[a] && bag[b] && (a!=b || bag[a]>=2));bag[a]--;bag[b]--;
  uint8_t result=eg_recipe(a,b);assert(result);bag[result]++;crafts++;return result;
}
static void use(const eg_land_node *n,uint8_t piece,uint8_t u,uint8_t v) {
  eg_room r;gather();assert(bag[piece]);
  uint8_t stand=255;
  for(unsigned d=0;d<4;d++) {
    unsigned x=u+(d==0 ? -1 : d==1 ? 1 : 0),y=v+(d==2 ? -1 : d==3 ? 1 : 0);
    if(x<128u && y<128u && seen[x][y])stand=0;
  }
  assert(stand==0);extract(&r,n);
  assert(eg_apply(&r,piece,local(u,v),0,0));bag[piece]--;eg_shutters(&r,0,0);apply_room(&r);
}
static void push(const eg_land_node *n,uint8_t u,uint8_t v,int du,int dv) {
  eg_room r;gather();assert(seen[u-du][v-dv]);extract(&r,n);
  assert(eg_push_block(&r,local(u,v),local(u+du,v+dv)));pushes++;
  eg_shutters(&r,0,0);apply_room(&r);
}
static void solve(uint8_t tool) {
  eg_land_cell cell;memset(bag,0,sizeof bag);
  for(unsigned u=0;u<EL_LENGTH(&p);u++)for(unsigned v=0;v<EL_CROSS(&p);v++) {
    eg_land_at(&p,EL_X(&p,u,v),EL_Y(&p,u,v),&cell);board[u][v]=cell.material;
  }
  gather();
  for(unsigned k=0;k<p.nnode;k++) {
    const eg_land_node *n=&p.node[k];uint8_t u=n->at,v=n->lane;
    gather();assert(seen[u-6u][v]);
    /* A preceding socket may not accidentally open an unrelated later cut. */
    if(n->kind!=EL_PORTAL)assert(!seen[u+2u][v]);
    if(n->kind==EL_LOAD) {
      if(n->condition>=EL_EITHER)push(n,u-3u,v+2u,-1,0);
      if(tool) {make(P_FIRE,P_MUD);use(n,P_BRICK,u-3u,v-1u);}
      else {push(n,u-3u,v+1u,0,-1);push(n,u-3u,v,0,-1);}
    } else if(n->kind==EL_HEAT)use(n,P_FIRE,u,v);
    else if(n->kind==EL_COLD) {make(P_AIR,P_WATER);make(P_AIR,P_RAIN);use(n,P_SNOW,u,v);}
    else if(n->kind==EL_POWER) {make(P_ENERGY,P_METAL);use(n,P_ELECTRIC,u-3u,v-1u);}
    else {
      eg_room r;extract(&r,n);assert(eg_portal_target(&r,local(u-2u,v),0,0)==local(u+2u,v));portals++;
    }
    gather();assert(seen[u+2u][v]);
  }
  uint8_t end=EL_LENGTH(&p)-3u;reach();assert(seen[end][eg_land_lane(&p,end)]);replays++;
}
int main(void) {
  unsigned plans=0,counts[6]={0},goals[5]={0},conds[4]={0},shapes[4]={0},designs[EL_DESIGNS]={0},vertical=0;
  eg_land invalid;memset(&p,0xA7,sizeof p);invalid=p;
  assert(!eg_land_plan(&p,1,EL_RULES,0,0) && !memcmp(&p,&invalid,sizeof p));
  assert(!eg_land_plan(&p,1,0,EL_SIZES,0) && !memcmp(&p,&invalid,sizeof p));
  assert(!eg_land_plan(&p,1,0,0,EL_BIOMES) && !memcmp(&p,&invalid,sizeof p));
  assert(!eg_land_plan(0,1,0,0,0));
  for(unsigned rule=0;rule<EL_RULES;rule++)for(unsigned size=0;size<EL_SIZES;size++)for(unsigned biome=0;biome<EL_BIOMES;biome++)for(unsigned seed=0;seed<32;seed++) {
    eg_land before,again;memset(&p,0xA7,sizeof p);before=p;
    if(!eg_land_plan(&p,(uint16_t)(seed*997u+biome*17u),rule,size,biome)) {assert(!memcmp(&p,&before,sizeof p));continue;}
    assert(eg_land_plan(&again,p.seed,rule,size,biome) && !memcmp(&p,&again,sizeof p));
    assert(p.nnode && p.nnode<=4 && p.nitem<=EL_ITEMS && p.width<=128 && p.height<=54);
    eg_land_cell edge;eg_land_at(&p,p.width,p.height,&edge);assert(EG_ABYSS(edge.material) && !edge.flags);
    counts[size]++;shapes[p.shape]++;vertical+=p.vertical;
    for(unsigned k=0;k<p.nnode;k++) {goals[p.node[k].kind]++;if(p.node[k].kind==EL_LOAD)conds[p.node[k].condition]++;}
    for(unsigned k=0;k<p.nitem;k++)for(unsigned j=k+1;j<p.nitem;j++)assert(p.item[k].u!=p.item[j].u || p.item[k].v!=p.item[j].v);
    solve(0);solve(1);plans++;
    for(unsigned y=0;y<p.height;y++)for(unsigned x=0;x<p.width;x++) {
      eg_land_cell a,b;eg_land_at(&p,x,y,&a);eg_land_at(&p,x,y,&b);assert(!memcmp(&a,&b,sizeof a));
      assert(a.design<EL_DESIGNS);designs[a.design]++;
      if(a.flags&EL_PROTECTED)assert(a.design==EL_FLOOR || a.design==EL_PAD || a.design==EL_RIVER);
    }
  }
  for(unsigned i=0;i<6;i++)assert(counts[i]);for(unsigned i=0;i<5;i++)assert(goals[i]);
  for(unsigned i=0;i<4;i++)assert(conds[i] && shapes[i]);for(unsigned i=0;i<EL_DESIGNS;i++)assert(designs[i]);assert(vertical);
  printf("land codec: %u plans, %u finite-resource global replays, %u pushes, %u recipes, %u powered crossings; 6 sizes, 4 shapes, 8 biomes, all conditions and environment families\n",plans,replays,pushes,crafts,portals);
  return 0;
}
