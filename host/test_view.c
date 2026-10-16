#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "core/eg_view.h"
uint8_t eg_window_packet[9];
static uint8_t model[54][128][3],cache[54][128][3];
static uint16_t rng=0x8E91u;
static unsigned transfers,seams;
static uint8_t next(void) {rng^=(uint16_t)(rng<<7);rng^=rng>>9;rng^=(uint16_t)(rng<<8);return (uint8_t)rng;}
static void store(void *ctx,uint8_t x,uint8_t y,uint8_t *p) {
  (void)ctx;assert(x<=124u && !(x&3u) && y<54u);transfers++;
  for(unsigned k=0;k<4;k++) {cache[y][x+k][0]=p[k];cache[y][x+k][1]=p[k+4];cache[y][x+k][2]=(uint8_t)(((p[8]>>k)&1u)|(((p[8]>>(4+k))&1u)<<1));}
}
static void load(void *ctx,uint8_t x,uint8_t y,uint8_t *p) {
  (void)ctx;assert(x<=124u && !(x&3u) && y<54u);transfers++;p[8]=0;
  for(unsigned k=0;k<4;k++) {p[k]=cache[y][x+k][0];p[k+4]=cache[y][x+k][1];p[8]|=(uint8_t)(((cache[y][x+k][2]&1u)<<k)|(((cache[y][x+k][2]>>1)&1u)<<(4+k)));}
}
int main(void) {
  static const uint8_t dims[6][2]={{16,14},{28,18},{44,26},{60,34},{88,42},{128,54}};
  for(unsigned cell=0;cell<256;cell++)for(unsigned d=0;d<4;d++) {
    int dx=d==0 ? 4 : d==1 ? -4 : 0,dy=d==2 ? 4 : d==3 ? -4 : 0;
    int x=(int)(cell&15)-dx,y=(int)(cell>>4)-dy;
    unsigned want=cell>=224 || x<0 || x>=16 || y<0 || y>=14 ? 255u : (unsigned)(y*16+x);
    assert(eg_view_rebase(cell,dx,dy)==want);
  }
  for(unsigned seed=0;seed<256;seed++)for(unsigned d=0;d<4;d++)for(unsigned fill=0;fill<2;fill++) {
    uint8_t bits[28],before[28];int dx=d==0 ? 4 : d==1 ? -4 : 0,dy=d==2 ? 4 : d==3 ? -4 : 0;
    for(unsigned i=0;i<28;i++)bits[i]=before[i]=(uint8_t)(seed*17u+i*31u);
    eg_view_bits(bits,dx,dy,fill);
    for(unsigned y=0;y<14;y++)for(unsigned x=0;x<16;x++) {
      int ox=(int)x+dx,oy=(int)y+dy;unsigned c=y*16+x;
      unsigned want=ox<0 || ox>=16 || oy<0 || oy>=14 ? fill : ((before[(oy*16+ox)/8]>>((oy*16+ox)&7))&1u);
      assert(((bits[c/8]>>(c&7))&1u)==want);
    }
  }
  for(unsigned profile=0;profile<6;profile++) {
    eg_view v={0,0,dims[profile][0],dims[profile][1]};eg_room r,old;uint8_t skin[224],crack[28],dir=0;
    memset(&r,0xA7,sizeof r);memset(crack,0,sizeof crack);
    for(unsigned y=0;y<v.height;y++)for(unsigned x=0;x<v.width;x++) {
      for(unsigned k=0;k<3;k++)model[y][x][k]=cache[y][x][k]=next();
      model[y][x][2]&=3;cache[y][x][2]&=3;
    }
    assert(eg_view_load(&v,&r,skin,crack,load,0));
    for(unsigned step=0;step<256;step++) {
      for(unsigned k=0;k<17;k++) {
        unsigned c=next()%224u,x=v.x+EG_X(c),y=v.y+EG_Y(c),flags=next()&3u,mask=1u<<(c&7u);
        model[y][x][0]=r.c[c]=next();model[y][x][1]=skin[c]=next();model[y][x][2]=flags;
        r.prot[c>>3]&=(uint8_t)~mask;crack[c>>3]&=(uint8_t)~mask;
        if(flags&1u)r.prot[c>>3]|=mask;if(flags&2u)crack[c>>3]|=mask;
      }
      if(profile) {
        int8_t dx=0,dy=0;
        if((dir==0 && v.x==v.width-16) || (dir==1 && v.y==v.height-14) ||
           (dir==2 && !v.x) || (dir==3 && !v.y))dir=(dir+1)&3;
        if(dir==0)dx=4;else if(dir==1)dy=4;else if(dir==2)dx=-4;else dy=-4;
        if(step&1u)assert(eg_view_flush(&v,&r,skin,crack,store,0));
        assert(eg_view_shift(&v,&r,skin,crack,dx,dy,(step&1u) ? 0 : store,load,0));seams++;
      } else {
        assert(eg_view_flush(&v,&r,skin,crack,store,0));assert(eg_view_load(&v,&r,skin,crack,load,0));
      }
      for(unsigned y=0;y<v.height;y++)for(unsigned x=0;x<v.width;x++) {
        unsigned c=eg_view_local(&v,x,y);
        if(x<v.x || x>=v.x+16u || y<v.y || y>=v.y+14u) {assert(c==255u);assert(!memcmp(cache[y][x],model[y][x],3));}
        else {
          assert(c==EG_IDX(x-v.x,y-v.y));
          unsigned flags=(EG_PROT(&r,c) ? 1u : 0u)|(crack[c>>3]&(1u<<(c&7u)) ? 2u : 0u);
          assert(r.c[c]==model[y][x][0] && skin[c]==model[y][x][1] && flags==model[y][x][2]);
        }
      }
      old=r;eg_view before=v;unsigned count=transfers;
      assert(!eg_view_shift(&v,&r,skin,crack,4,4,store,load,0));
      assert(!eg_view_shift(&v,&r,skin,crack,1,0,store,load,0));
      assert(!memcmp(&r,&old,sizeof r) && !memcmp(&v,&before,sizeof v) && count==transfers);
      for(unsigned i=224u;i<sizeof r;i++)if(i<offsetof(eg_room,prot) || i>=offsetof(eg_room,prot)+28u)assert(((uint8_t *)&r)[i]==0xA7u);
    }
    assert(eg_view_flush(&v,&r,skin,crack,store,0));
    for(unsigned y=0;y<v.height;y++)for(unsigned x=0;x<v.width;x++)assert(!memcmp(cache[y][x],model[y][x],3));
  }
  printf("2D view: %u seams, 26112 mutations, six footprints, exact active/inactive cells, %u transfers, untouched metadata\n",seams,transfers);
  return 0;
}
