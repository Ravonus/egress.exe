#ifndef LAND_BIND_CASES_H
#define LAND_BIND_CASES_H
#include "land_cases.h"
#include "core/eg_land_bind.h"
#ifdef __SDCC
#define LB_LOCAL NONBANKED
#else
#define LB_LOCAL
#endif
typedef struct {eg_land p;uint8_t pattern;} lb_context;
static uint8_t lb_read(void *context,uint8_t x,uint8_t y) LB_LOCAL {
  lb_context *ctx=context;eg_land_cell c;uint8_t k,u=ctx->p.vertical ? y : x,v=ctx->p.vertical ? x : y;
  eg_land_at(&ctx->p,x,y,&c);
  for(k=0u;k<ctx->p.nnode;k++) {
    const eg_land_node *n=&ctx->p.node[k];
    if(n->kind!=EL_LOAD && n->kind!=EL_POWER)continue;
    if(u==n->at-3u && v==n->lane-1u)
      return ctx->pattern&1u ? (n->kind==EL_LOAD ? T_BLOCK_PLATE : T_SOCKET_ON) : (n->kind==EL_LOAD ? T_PLATE : T_SOCKET);
    if(n->kind==EL_LOAD && n->condition>=EL_EITHER && u==n->at-4u && v==n->lane+2u)
      return ctx->pattern&2u ? T_BLOCK_PLATE : T_PLATE;
    if(u==n->at && v==n->lane)return (uint8_t)(T_GATE|(ctx->pattern&8u ? F_OB : 0u));
  }
  return c.material;
}
static void lb_step(lb_context *ctx,eg_room *r,uint8_t step) {
  eg_view v;uint8_t x,y;
  ctx->pattern=step;v.width=ctx->p.width;v.height=ctx->p.height;
  v.x=(uint8_t)(4u*((step*7u)%(1u+(v.width-16u)/4u)));
  v.y=(uint8_t)(4u*((step*3u)%(1u+(v.height-14u)/4u)));
  memset(r,0xA7u,sizeof *r);
  for(y=0u;y<14u;y++)for(x=0u;x<16u;x++)r->c[EG_IDX(x,y)]=lb_read(ctx,v.x+x,v.y+y);
  eg_land_bind(&ctx->p,&v,r,(uint8_t)((step>>2)&3u),lb_read,ctx);
}
#endif
