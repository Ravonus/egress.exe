#ifndef VIEW_CASES_H
#define VIEW_CASES_H
#include <string.h>
#include "core/eg_view.h"
#ifdef __SDCC
#include "view_cache.h"
#endif
#define VC_CASES 3u
#define VC_STEPS 96u
#define VC_BYTES 716u
static uint16_t vc_rng;
static uint8_t vc_dir;
static uint8_t vc_bits[5][28],vc_serial;
static uint8_t vc_next(void) {vc_rng^=(uint16_t)(vc_rng<<7);vc_rng^=vc_rng>>9;vc_rng^=(uint16_t)(vc_rng<<8);return (uint8_t)vc_rng;}
static uint8_t vc_begin(uint8_t which,eg_view *v,eg_room *r,uint8_t *skin,uint8_t *crack,
                        eg_view_io store,eg_view_io load) {
  uint8_t x,y,k,flags;
  v->x=v->y=vc_dir=vc_serial=0u;v->width=which==0u ? 32u : which==1u ? 64u : 128u;
  v->height=which==0u ? 18u : which==1u ? 34u : 54u;
  vc_rng=(uint16_t)(0x8E91u+which*997u);memset(r,0xA7u,sizeof *r);memset(crack,0u,28u);
  for(y=0u;y<5u;y++)for(x=0u;x<28u;x++)vc_bits[y][x]=(uint8_t)(which*17u+y*53u+x*31u);
  for(y=0u;y<v->height;y++)for(x=0u;x<v->width;x+=4u) {
    eg_window_packet[8]=0u;
    for(k=0u;k<4u;k++) {
      eg_window_packet[k]=vc_next();eg_window_packet[k+4u]=vc_next();flags=vc_next()&3u;
      eg_window_packet[8]|=(uint8_t)(((flags&1u)<<k)|(((flags>>1)&1u)<<(4u+k)));
    }
    store(0,x,y,eg_window_packet);
  }
  return eg_view_load(v,r,skin,crack,load,0);
}
static uint8_t vc_step(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *crack,eg_view_io store,eg_view_io load) {
  uint8_t k,c,mask,flags;int8_t dx=0,dy=0;
  for(k=0u;k<17u;k++) {
    c=(uint8_t)(vc_next()%EG_N);mask=(uint8_t)(1u<<(c&7u));flags=vc_next()&3u;
    r->c[c]=vc_next();skin[c]=vc_next();r->prot[c>>3]&=(uint8_t)~mask;crack[c>>3]&=(uint8_t)~mask;
    if(flags&1u)r->prot[c>>3]|=mask;if(flags&2u)crack[c>>3]|=mask;
  }
  if((vc_dir==0u && v->x==v->width-EG_W) || (vc_dir==1u && v->y==v->height-EG_H) ||
     (vc_dir==2u && !v->x) || (vc_dir==3u && !v->y))vc_dir=(uint8_t)((vc_dir+1u)&3u);
  if(vc_dir==0u)dx=4;else if(vc_dir==1u)dy=4;else if(vc_dir==2u)dx=-4;else dy=-4;
  for(k=0u;k<5u;k++) {
    for(c=0u;c<28u;c++)vc_bits[k][c]^=(uint8_t)(vc_serial*17u+c*31u+k);
    eg_view_bits(vc_bits[k],dx,dy,(uint8_t)(k==1u));
  }
  vc_serial++;
#ifdef __SDCC
  load;
  if(vc_serial&1u) {
    if(!eg_view_flush(v,r,skin,crack,store,0))return 0u;
    return eg_view_shift_prepared(v,r,skin,crack,dx,dy);
  }
  return eg_view_shift_cached(v,r,skin,crack,dx,dy);
#else
  return eg_view_shift(v,r,skin,crack,dx,dy,store,load,0);
#endif
}
static void vc_capture(const eg_view *v,const eg_room *r,const uint8_t *skin,const uint8_t *crack,uint8_t *out) {
  memcpy(out,r,320u);memcpy(out+320u,skin,224u);memcpy(out+544u,crack,28u);memcpy(out+572u,v,4u);
  memcpy(out+576u,vc_bits,140u);
}
#endif
