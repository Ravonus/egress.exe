#ifndef EW_CASES_H
#define EW_CASES_H
#include <string.h>
#include "core/eg_window.h"
#define WC_STEPS 64u
#define WC_BYTES 508u
static uint16_t wc_rng;
static uint8_t wc_next(void) {wc_rng^=wc_rng<<7;wc_rng^=wc_rng>>9;wc_rng^=wc_rng<<8;return (uint8_t)wc_rng;}
static void wc_begin(eg_room *r,uint8_t *skin,uint8_t *crack,eg_window_io store,void *ctx) {
  uint8_t x,y,k,c,v,packed[9];
  wc_rng=0x8E91u;memset(r,0,sizeof *r);memset(crack,0,28u);
  for(y=0u;y<EG_H;y++)for(x=0u;x<EW_WIDTH;x+=4u) {
    packed[8]=0u;
    for(k=0u;k<4u;k++) {
      packed[k]=wc_next();packed[k+4u]=wc_next();v=wc_next()&3u;
      packed[8]|=(uint8_t)(((v&1u)<<k)|(((v>>1)&1u)<<(4u+k)));
      if(x<EG_W) {
        c=EG_IDX(x+k,y);r->c[c]=packed[k];skin[c]=packed[k+4u];
        if(v&EW_PROTECTED)r->prot[c>>3]|=(uint8_t)(1u<<(c&7u));
        if(v&EW_CRACKED)crack[c>>3]|=(uint8_t)(1u<<(c&7u));
      }
    }
    store(ctx,x,y,packed);
  }
}
static uint8_t wc_step(uint8_t step,eg_room *r,uint8_t *skin,uint8_t *crack,uint8_t *origin,
                       eg_window_io store,eg_window_io load,void *ctx) {
  uint8_t k,c,v,flags;
  for(k=0u;k<17u;k++) {
    c=(uint8_t)(wc_next()%EG_N);v=(uint8_t)(1u<<(c&7u));flags=wc_next()&3u;
    r->c[c]=wc_next();skin[c]=wc_next();r->prot[c>>3]&=(uint8_t)~v;crack[c>>3]&=(uint8_t)~v;
    if(flags&EW_PROTECTED)r->prot[c>>3]|=v;if(flags&EW_CRACKED)crack[c>>3]|=v;
  }
#ifdef __SDCC
  store;load;ctx;
  return eg_window_shift_cached(r,skin,crack,origin,step&8u ? -1 : 1);
#else
  return eg_window_shift(r,skin,crack,origin,step&8u ? -1 : 1,store,load,ctx);
#endif
}
static void wc_capture(const eg_room *r,const uint8_t *skin,const uint8_t *crack,uint8_t origin,uint8_t step,uint8_t *out) {
  memcpy(out,r->c,224u);memcpy(out+224u,skin,224u);memcpy(out+448u,r->prot,28u);memcpy(out+476u,crack,28u);
  out[504u]=origin;out[505u]=step;out[506u]=out[507u]=0u;
}
#endif
