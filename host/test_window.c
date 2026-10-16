#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "core/eg_window.h"
static uint8_t backing[EG_H][EW_WIDTH][3],model[EG_H][EW_WIDTH][3];
uint8_t eg_window_packet[9];
static unsigned reads,writes;
static void store(void *ctx,uint8_t x,uint8_t y,uint8_t *p) {
  (void)ctx;assert(x<=EW_WIDTH-EW_STEP && y<EG_H);
  for(unsigned k=0;k<4;k++) {backing[y][x+k][0]=p[k];backing[y][x+k][1]=p[4+k];backing[y][x+k][2]=(uint8_t)(((p[8]>>k)&1u)|(((p[8]>>(4+k))&1u)<<1));}writes++;
}
static void load(void *ctx,uint8_t x,uint8_t y,uint8_t *p) {
  (void)ctx;assert(x<=EW_WIDTH-EW_STEP && y<EG_H);p[8]=0u;
  for(unsigned k=0;k<4;k++) {p[k]=backing[y][x+k][0];p[4+k]=backing[y][x+k][1];p[8]|=(uint8_t)(((backing[y][x+k][2]&1u)<<k)|(((backing[y][x+k][2]>>1)&1u)<<(4+k)));}reads++;
}
static uint16_t rng=0x8E91u;
static uint8_t next(void) {rng^=rng<<7;rng^=rng>>9;rng^=rng<<8;return (uint8_t)rng;}
int main(void) {
  eg_room r,before;uint8_t skin[EG_N],crack[EG_N/8],origin=0u,x,y,c,k,v,flags;int8_t dir=1;
  memset(&r,0,sizeof r);memset(crack,0,sizeof crack);
  for(y=0u;y<EG_H;y++)for(x=0u;x<EW_WIDTH;x++) {
    model[y][x][0]=backing[y][x][0]=next();model[y][x][1]=backing[y][x][1]=next();
    model[y][x][2]=backing[y][x][2]=next()&3u;
    if(x<EG_W) {c=EG_IDX(x,y);r.c[c]=model[y][x][0];skin[c]=model[y][x][1];
      if(model[y][x][2]&EW_PROTECTED)r.prot[c>>3]|=(uint8_t)(1u<<(c&7));
      if(model[y][x][2]&EW_CRACKED)crack[c>>3]|=(uint8_t)(1u<<(c&7));}
  }
  for(unsigned step=0u;step<256u;step++) {
    for(k=0u;k<17u;k++) {
      c=(uint8_t)(next()%EG_N);x=EG_X(c);y=EG_Y(c);v=(uint8_t)(1u<<(c&7u));flags=next()&3u;
      r.c[c]=next();skin[c]=next();r.prot[c>>3]&=(uint8_t)~v;crack[c>>3]&=(uint8_t)~v;
      if(flags&EW_PROTECTED)r.prot[c>>3]|=v;if(flags&EW_CRACKED)crack[c>>3]|=v;
      model[y][origin+x][0]=r.c[c];model[y][origin+x][1]=skin[c];model[y][origin+x][2]=flags;
    }
    if(origin==EW_WIDTH-EG_W)dir=-1;else if(!origin)dir=1;
    assert(eg_window_shift(&r,skin,crack,&origin,dir,store,load,0));
    for(c=0u;c<EG_N;c++) {
      x=EG_X(c);y=EG_Y(c);flags=(uint8_t)((EG_PROT(&r,c) ? EW_PROTECTED : 0u)|
        (crack[c>>3]&(1u<<(c&7u)) ? EW_CRACKED : 0u));
      assert(r.c[c]==model[y][origin+x][0] && skin[c]==model[y][origin+x][1] && flags==model[y][origin+x][2]);
    }
    for(y=0u;y<EG_H;y++)for(x=0u;x<EW_WIDTH;x++) {
      uint8_t local=eg_window_local(origin,EW_IDX(x,y));
      assert(local==(x>=origin && x<origin+EG_W ? EG_IDX(x-origin,y) : EG_LINK_NONE));
      if(x<origin || x>=origin+EG_W)assert(!memcmp(backing[y][x],model[y][x],3u));
    }
  }
  before=r;k=(uint8_t)reads;v=origin;
  assert(!eg_window_shift(&r,skin,crack,&origin,0,store,load,0));
  assert(!memcmp(&r,&before,sizeof r) && origin==v && (uint8_t)reads==k);
  printf("window: 256 seams, 4352 mutations, exact active/inactive state, %u bounded strip transfers\n",reads+writes);
  return 0;
}
