#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_land_bind.h"
static uint8_t at(const eg_land *p,const eg_view *v,uint8_t u,uint8_t y) {
  uint8_t x=EL_X(p,u,y),gy=EL_Y(p,u,y);
  if(x<v->x || gy<v->y)return EG_LINK_NONE;
  x=(uint8_t)(x-v->x);gy=(uint8_t)(gy-v->y);
  return x<EG_W && gy<EG_H ? EG_IDX(x,gy) : EG_LINK_NONE;
}
static uint8_t weight(uint8_t raw) {
  return raw==T_SOCKET_ON || raw==T_BLOCK_PLATE || raw==(T_PLATE|F_OB) ||
    (raw&(T_MASK|F_PLATE))==(T_ROCK|F_PLATE);
}
static uint8_t source(const eg_land *p,const eg_view *v,uint8_t u,uint8_t y,
                       uint8_t *rule,uint8_t bit,eg_land_read read,void *ctx) {
  uint8_t c=at(p,v,u,y);
  if(c==EG_LINK_NONE) {
    c=EG_LINK_EXTERNAL;
    if(weight(read(ctx,EL_X(p,u,y),EL_Y(p,u,y))))*rule|=bit;
  }
  return c;
}
void eg_land_bind_frame(const eg_land *p,const eg_view *v,eg_land_frame *r,uint8_t latched,
                  eg_land_read read,void *context) EG_BANKED {
  uint8_t k,cell,start_lane,end_lane;eg_link *l;const eg_land_node *n;const eg_land_item *it;
#ifdef __SDCC
  static uint16_t seed;
  static uint8_t length,cross,start,end;
  if(seed!=p->seed || length!=EL_LENGTH(p) || cross!=EL_CROSS(p)) {
    seed=p->seed;length=EL_LENGTH(p);cross=EL_CROSS(p);
    start=eg_land_lane(p,2u);end=eg_land_lane(p,(uint8_t)(length-3u));
  }
  start_lane=start;end_lane=end;
#else
  start_lane=eg_land_lane(p,2u);end_lane=eg_land_lane(p,(uint8_t)(EL_LENGTH(p)-3u));
#endif
  memset(&r->nlink,0,12u);r->nitem=0u;
  r->start=at(p,v,2u,start_lane);
  k=(uint8_t)(EL_LENGTH(p)-3u);r->door=at(p,v,k,end_lane);
  for(k=0u;k<p->nitem;k++) {
    it=&p->item[k];cell=at(p,v,it->u,it->v);
    if(cell<EG_N && r->nitem<EG_MAX_ITEM) {
      r->item_cell[r->nitem]=cell;r->item_piece[r->nitem++]=it->piece;
    }
  }
  for(k=0u;k<p->nnode;k++) {
    n=&p->node[k];
    if(n->kind==EL_LOAD || n->kind==EL_POWER) {
      l=&r->link[r->nlink];
      l->rule=n->condition==EL_LATCH ? EG_LINK_LATCH : n->condition==EL_EITHER ? EG_LINK_ANY : 0u;
      l->gate=at(p,v,n->at,n->lane);
      if(l->gate==EG_LINK_NONE)l->gate=EG_LINK_EXTERNAL;
      if(l->rule&EG_LINK_LATCH) {
        if(l->gate==EG_LINK_EXTERNAL) {
          if((latched&(1u<<r->nlink)) || (read(context,EL_X(p,n->at,n->lane),EL_Y(p,n->at,n->lane))&T_MASK)==T_GATE_OPEN)
            l->rule|=EG_LINK_EXT_LATCH;
        }
      }
      l->a=source(p,v,n->at-3u,n->lane-1u,&l->rule,EG_LINK_EXT_A,read,context);
      l->b=EG_LINK_NONE;
      if(n->kind==EL_LOAD && n->condition>=EL_EITHER)
        l->b=source(p,v,n->at-4u,n->lane+2u,&l->rule,EG_LINK_EXT_B,read,context);
      r->nlink++;
    } else if(n->kind==EL_PORTAL) {
      r->portal[0]=at(p,v,n->at-2u,n->lane);r->portal[1]=at(p,v,n->at+2u,n->lane);
      r->portal_need=EG_PORTAL_ON|1u;
    }
  }
}

void eg_land_apply_frame(eg_room *r,const eg_land_frame *f,uint8_t latched) EG_BANKED {
  uint8_t k,g;
  r->start=f->start;r->door=f->door;r->nitem=f->nitem;
  for(k=0u;k<f->nitem;k++) {r->item_cell[k]=f->item_cell[k];r->item_piece[k]=f->item_piece[k];}
  memcpy(&r->nlink,&f->nlink,12u);
  for(k=0u;k<r->nlink;k++)if((r->link[k].rule&EG_LINK_LATCH) && (latched&(1u<<k))) {
    g=r->link[k].gate;
    if(g<EG_N && (r->c[g]&T_MASK)==T_GATE)r->c[g]=(uint8_t)((r->c[g]&F_OB)|T_GATE_OPEN);
  }
}
void eg_land_bind(const eg_land *p,const eg_view *v,eg_room *r,uint8_t latched,
                  eg_land_read read,void *context) EG_BANKED {
  eg_land_frame frame;
  eg_land_bind_frame(p,v,&frame,latched,read,context);
  eg_land_apply_frame(r,&frame,latched);
}
