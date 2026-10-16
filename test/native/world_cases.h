#ifndef WORLD_CASES_H
#define WORLD_CASES_H
#include <string.h>
#include "core/eg_land_bind.h"
#include "land_cases.h"
#define WG_STEPS 33u
#define WG_BYTES 580u
static uint8_t wg_direction;
static const eg_land *wg_p(void);
static eg_view *wg_v(void);
static eg_room *wg_r(void);
static uint8_t *wg_skin(void);
static uint8_t *wg_crack(void);
static uint8_t wg_latched(void);
static uint8_t wg_begin(uint8_t which);
static uint8_t wg_read(uint8_t x,uint8_t y);
static void wg_write(uint8_t x,uint8_t y,uint8_t raw);
static void wg_bind(void);
static void wg_source(uint8_t occupied);
static void wg_load(uint8_t x,uint8_t y);
static uint8_t wg_shift(int8_t dx,int8_t dy);
static uint8_t wg_step(uint8_t which,uint8_t step) {
  const eg_land *p=wg_p();eg_view *v=wg_v();uint8_t k,u,w,x,y;int8_t dx=0,dy=0;
  if(step==32u) {wg_direction=0u;return wg_begin(which);}
  if((step&3u)==0u || (step&3u)==3u) {
    const eg_land_node *n=&p->node[(step>>2)%p->nnode];u=n->at-3u;w=n->lane-1u;
    x=EL_X(p,u,w);y=EL_Y(p,u,w);
    wg_write(x,y,(step&3u)==0u ? (n->kind==EL_POWER ? T_SOCKET_ON : T_BLOCK_PLATE) : (n->kind==EL_POWER ? T_SOCKET : T_PLATE));
    wg_bind();
    if((step&3u)==3u) {
      uint8_t local=255u;
      if(x>=v->x && x<v->x+16u && y>=v->y && y<v->y+14u)local=EG_IDX(x-v->x,y-v->y);
      wg_source(local);wg_bind();
    }
    return (uint8_t)(wg_read(x,y)^wg_read(255u,255u));
  }
  for(k=0u;k<4u;k++) {
    if((wg_direction==0u && v->x==v->width-16u) || (wg_direction==1u && v->y==v->height-14u) ||
       (wg_direction==2u && !v->x) || (wg_direction==3u && !v->y))wg_direction=(uint8_t)((wg_direction+1u)&3u);
    else break;
  }
  if(k==4u)return 0u;
  if(wg_direction==0u)dx=4;else if(wg_direction==1u)dy=4;else if(wg_direction==2u)dx=-4;else dy=-4;
  if((step&3u)==2u) {
    if(dx) {
      x=dx>0 ? v->x+16u : v->x-4u;
      for(y=0u;y<14u;y++)wg_load(x,v->y+y);
    } else {
      y=dy>0 ? v->y+14u : v->y-4u;
      for(k=0u;k<4u;k++)for(x=0u;x<16u;x+=4u)wg_load(v->x+x,y+k);
    }
  }
  return wg_shift(dx,dy);
}
static void wg_capture(uint8_t result,uint8_t step,uint8_t *out) {
  memcpy(out,wg_r(),320u);memcpy(out+320u,wg_skin(),224u);memcpy(out+544u,wg_crack(),28u);
  memcpy(out+572u,wg_v(),4u);out[576u]=wg_latched();out[577u]=1u;out[578u]=result;out[579u]=step;
}
#endif
