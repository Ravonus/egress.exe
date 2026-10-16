/* Global, finite-resource witness replay. Every mutation is made by the same
 * 16x14 push/circuit/recipe core used by the cartridge. Global reachability is
 * rechecked between actions; reserved floor alone is not a solution proof. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/eg_land.h"
#include "core/eg_land_bind.h"
#include "core/eg_land_dead.h"
#include "core/eg_piece.h"
static eg_land p;
#undef assert
#define assert(c) do {if(!(c)) {fprintf(stderr,"land assertion %s:%u: %s (seed %u rule %u size %u biome %u %ux%u vertical %u)\n",__FILE__,__LINE__,#c,p.seed,p.rule,p.size,p.biome,p.width,p.height,p.vertical);exit(1);}}while(0)
static uint8_t board[128][128],seen[128][128],bag[P_COUNT];
static uint16_t queue[16384];
static uint8_t ou,ov;
static unsigned replays,pushes,crafts,portals;
static unsigned bindings;
static unsigned dead_checks;
static uint8_t jumping,portal_enabled=1u;
static uint8_t read_at(uint8_t u,uint8_t v) {return board[u][v];}
static uint8_t read_world(void *ctx,uint8_t x,uint8_t y) {(void)ctx;return board[p.vertical ? y : x][p.vertical ? x : y];}
static uint8_t known_world(void *ctx,uint8_t x,uint8_t y) {(void)ctx;return x<p.width && y<p.height;}
static void quad_world(void *ctx,uint8_t x,uint8_t y,uint8_t *out) {for(unsigned k=0;k<4;k++)out[k]=read_world(ctx,x+k,y);}
static uint8_t mask_world(void *ctx,uint8_t pair,uint8_t byte) {
  uint8_t result=0;unsigned y=pair*2u+byte/4u;(void)ctx;
  for(unsigned k=0;k<8;k++)if((byte%4u)*32u+k*4u<p.width && y<p.height)result|=1u<<k;
  return result;
}
static uint8_t dead_result(void) {
  eg_land_dead s;eg_room r;eg_view v={0,0,p.width,p.height};uint8_t units[24],n=0,result=255;
  const eg_land_dead_io io={known_world,read_world,quad_world,mask_world,0};
  memset(&r,0,sizeof r);
  for(unsigned y=0;y<14;y++)for(unsigned x=0;x<16;x++)r.c[EG_IDX(x,y)]=read_world(0,x,y);
  for(unsigned i=1;i<P_COUNT;i++)for(unsigned k=0;k<bag[i];k++) {assert(n<24);units[n++]=i;}
  eg_land_dead_begin(&s);
  for(unsigned steps=0;steps<2000 && result==255;steps++)result=eg_land_dead_step(&s,&p,&v,&r,255,units,n,0,&io,4);
  assert(result!=255);dead_checks++;return result;
}
static void make_board(void) {
  eg_land_cell c;
  for(unsigned u=0;u<EL_LENGTH(&p);u++)for(unsigned v=0;v<EL_CROSS(&p);v++) {
    eg_land_at(&p,EL_X(&p,u,v),EL_Y(&p,u,v),&c);board[u][v]=c.material;
  }
}
static void dead_cases(void) {
  assert(eg_land_plan(&p,17,0,0,0));p.nnode=1;p.nitem=0;p.vertical=0;
  for(unsigned kind=EL_LOAD;kind<=EL_POWER;kind++) {
    p.node[0].kind=kind;p.node[0].condition=EL_HOLD;p.node[0].alternatives=0;
    make_board();
    for(unsigned u=0;u<p.width;u++)for(unsigned v=0;v<p.height;v++)if(EG_CRATE(board[u][v]) || board[u][v]==T_ITEM)board[u][v]=T_EMPTY;
    memset(bag,0,sizeof bag);assert(dead_result());
    const eg_land_node *n=&p.node[0];
    if(kind==EL_LOAD) {
      board[1][n->lane]=T_BLOCK;assert(dead_result());board[1][n->lane]=T_EMPTY;
      board[n->at-3u][n->lane+1u]=T_BLOCK;assert(!dead_result());board[n->at-3u][n->lane+1u]=T_EMPTY;
      bag[P_BRICK]=1;assert(!dead_result());bag[P_BRICK]=0;
      bag[P_FIRE]=bag[P_MUD]=1;assert(!dead_result());memset(bag,0,sizeof bag);
      p.node[0].condition=EL_LATCH;assert(!dead_result());p.node[0].condition=EL_HOLD;
    } else if(kind==EL_HEAT) {
      bag[P_STEAM]=1;assert(!dead_result());bag[P_STEAM]=0;bag[P_BRICK]=1;assert(dead_result());bag[P_BRICK]=0;
      p.nitem=1;p.item[0].u=n->at-5u;p.item[0].v=n->lane-2u;p.item[0].piece=P_FIRE;
      board[p.item[0].u][p.item[0].v]=T_ITEM|F_OB;assert(!dead_result());
      board[p.item[0].u][p.item[0].v]=T_EMPTY;p.nitem=0;
    } else if(kind==EL_COLD) {
      bag[P_AIR]=bag[P_WATER]=1;assert(dead_result());bag[P_AIR]=2;assert(!dead_result());memset(bag,0,sizeof bag);
      bag[P_AIR]=bag[P_RAIN]=1;assert(!dead_result());memset(bag,0,sizeof bag);
    } else {
      bag[P_METAL]=1;assert(dead_result());bag[P_AIR]=bag[P_FIRE]=1;assert(!dead_result());memset(bag,0,sizeof bag);
    }
  }
}
static void bindings_check(void) {
  eg_view v;eg_room r;
  v.width=p.width;v.height=p.height;
  for(v.y=0;v.y<=p.height-14u;v.y+=4u)for(v.x=0;v.x<=p.width-16u;v.x+=4u) {
    memset(&r,0xA7,sizeof r);
    for(unsigned y=0;y<14;y++)for(unsigned x=0;x<16;x++)r.c[EG_IDX(x,y)]=read_world(0,v.x+x,v.y+y);
    eg_land_bind(&p,&v,&r,0,read_world,0);bindings++;
    unsigned count=0;
    for(unsigned k=0;k<p.nitem;k++) {
      int x=(int)EL_X(&p,p.item[k].u,p.item[k].v)-v.x,y=(int)EL_Y(&p,p.item[k].u,p.item[k].v)-v.y;
      if(x>=0 && x<16 && y>=0 && y<14) {
        assert(count<6 && r.item_cell[count]==EG_IDX(x,y) && r.item_piece[count]==p.item[k].piece);count++;
      }
    }
    assert(count==r.nitem);
    count=0;
    for(unsigned k=0;k<p.nnode;k++) {
      const eg_land_node *n=&p.node[k];
      if(n->kind!=EL_LOAD && n->kind!=EL_POWER)continue;
      uint8_t a=board[n->at-3u][n->lane-1u],b=n->condition>=EL_EITHER ? board[n->at-4u][n->lane+2u] : 0;
      a=(a==T_BLOCK_PLATE || a==T_SOCKET_ON);b=(b==T_BLOCK_PLATE);
      unsigned want=n->condition==EL_EITHER ? a|b : n->condition==EL_BOTH ? a&b : a;
      if(n->condition==EL_LATCH && board[n->at][n->lane]==T_GATE_OPEN)want=1;
      assert(eg_link_power(&r,count,0,0)==want);count++;
    }
    assert(count==r.nlink);for(unsigned k=0;k<28;k++)assert(r.prot[k]==0xA7);
  }
}
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
    if(jumping)for(int dx=-1;dx<=1;dx++)for(int dy=-1;dy<=1;dy++)if(dx || dy) {
      unsigned holes=0;
      for(int length=1;length<=3;length++) {
        int x=(int)u+dx*length,y=(int)v+dy*length;
        if(x<0 || y<0 || x>=EL_LENGTH(&p) || y>=EL_CROSS(&p))break;
        unsigned raw=board[x][y]&T_MASK;
        if(raw==T_PIT || raw==T_GLITCH) {holes++;continue;}
        if(holes && walk(raw) && !seen[x][y]) {seen[x][y]=1;queue[tail++]=(uint16_t)((x<<8)|y);}
        break;
      }
    }
    for(unsigned k=0;portal_enabled && k<p.nnode;k++)if(p.node[k].kind==EL_PORTAL) {
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
  assert(!dead_result());
  for(unsigned k=0;k<p.nnode;k++) {
    const eg_land_node *n=&p.node[k];uint8_t u=n->at,v=n->lane;
    gather();assert(seen[u-6u][v]);
    /* A preceding socket may not accidentally open an unrelated later cut. */
    if(n->kind!=EL_PORTAL)assert(!seen[u+2u][v]);
    /* Overapproximate short diagonal rolls as well as cardinal crossings.
     * Locks cannot acquire an accidental route around their island edge. */
    jumping=1u;portal_enabled=(uint8_t)(n->kind!=EL_PORTAL);reach();
    if(n->kind==EL_COLD && (n->alternatives&EL_JUMP))assert(seen[u+2u][v]);
    else assert(!seen[u+2u][v]);
    jumping=0u;portal_enabled=1u;
    if(n->kind==EL_LOAD) {
      if(n->condition>=EL_EITHER && (!tool || n->condition==EL_BOTH))push(n,u-3u,v+2u,-1,0);
      if(tool) {make(P_FIRE,P_MUD);use(n,P_BRICK,u-3u,v-1u);}
      else if(n->condition!=EL_EITHER) {push(n,u-3u,v+1u,0,-1);push(n,u-3u,v,0,-1);}
    } else if(n->kind==EL_HEAT)use(n,P_FIRE,u,v);
    else if(n->kind==EL_COLD) {
      if(tool && (n->alternatives&EL_JUMP))use(n,P_MUD,u,v+2u);
      else {make(P_AIR,P_RAIN);use(n,P_SNOW,u,v);}
    }
    else if(n->kind==EL_POWER) {make(P_ENERGY,P_METAL);use(n,P_ELECTRIC,u-3u,v-1u);}
    else {
      eg_room r;extract(&r,n);assert(eg_portal_target(&r,local(u-2u,v),0,0)==local(u+2u,v));portals++;
    }
    gather();assert(seen[u+2u][v]);
    assert(!dead_result());
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
  dead_cases();
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
    for(unsigned pattern=0;pattern<4;pattern++) {
      for(unsigned k=0;k<p.nnode;k++) {
        const eg_land_node *n=&p.node[k];if(n->kind!=EL_LOAD && n->kind!=EL_POWER)continue;
        board[n->at-3u][n->lane-1u]=pattern&1 ? (n->kind==EL_LOAD ? T_BLOCK_PLATE : T_SOCKET_ON) : (n->kind==EL_LOAD ? T_PLATE : T_SOCKET);
        if(n->kind==EL_LOAD && n->condition>=EL_EITHER)board[n->at-4u][n->lane+2u]=pattern&2 ? T_BLOCK_PLATE : T_PLATE;
      }
      bindings_check();
    }
    for(unsigned y=0;y<p.height;y++)for(unsigned x=0;x<p.width;x++) {
      eg_land_cell a,b;eg_land_at(&p,x,y,&a);eg_land_at(&p,x,y,&b);assert(!memcmp(&a,&b,sizeof a));
      assert(a.design<EL_DESIGNS);designs[a.design]++;
      if(a.flags&EL_PROTECTED)assert(a.design==EL_FLOOR || a.design==EL_PAD || a.design==EL_RIVER);
    }
  }
  for(unsigned i=0;i<6;i++)assert(counts[i]);for(unsigned i=0;i<5;i++)assert(goals[i]);
  for(unsigned i=0;i<4;i++)assert(conds[i] && shapes[i]);for(unsigned i=0;i<EL_DESIGNS;i++)assert(designs[i]);assert(vertical);
  printf("land codec: %u plans, %u finite-resource global replays, %u pushes, %u recipes, %u powered crossings; 6 sizes, 4 shapes, 8 biomes, all conditions and environment families\n",plans,replays,pushes,crafts,portals);
  printf("world bindings: %u windows, all visible item identities and off-window circuit truth states\n",bindings);
  printf("global dead-room certificates: %u checks, valid witness prefixes retained and irreversible resource/weight cases detected\n",dead_checks);
  return 0;
}
