#include <stdio.h>
#include <string.h>
#include "../test/native/world_cases.h"
uint8_t eg_window_packet[9];
static eg_land p;
static eg_view v;
static eg_room r;
static uint8_t skin[224],crack[28],latched;
static uint8_t full[54][128][3],cached[54][32];
static const eg_land *wg_p(void) {return &p;}
static eg_view *wg_v(void) {return &v;}
static eg_room *wg_r(void) {return &r;}
static uint8_t *wg_skin(void) {return skin;}
static uint8_t *wg_crack(void) {return crack;}
static uint8_t wg_latched(void) {return latched;}
static uint8_t skin_of(uint8_t design,uint8_t variant) {
  static const uint8_t ids[6][2]={{31,33},{35,38},{39,40},{41,42},{43,44},{45,46}};
  return design>=1 && design<=6 ? (uint8_t)(ids[design-1][variant&1]+1u) : 0u;
}
static uint8_t clean(uint8_t s,uint8_t raw) {
  unsigned t=raw&31u;if(!s)return s;s--;
  if(s==31 || s==33 || s==35 || s==38 || s==39 || s==40)return t==T_DIRT ? s+1 : 0;
  if(s==41 || s==42)return t==T_EMPTY ? s+1 : 0;
  if(s>=43 && s<=46)return t==T_WATER ? s+1 : 0;
  return s+1;
}
static unsigned local(unsigned x,unsigned y) {return x>=v.x && y>=v.y && x<v.x+16u && y<v.y+14u ? (y-v.y)*16u+x-v.x : 255u;}
static uint8_t wg_read(uint8_t x,uint8_t y) {
  if(x>=v.width || y>=v.height)return T_PIT|F_ABYSS;
  unsigned c=local(x,y);if(c<224)return r.c[c];cached[y][x/4u]=1;return full[y][x][0];
}
static uint8_t read_global(void *ctx,uint8_t x,uint8_t y) {(void)ctx;return wg_read(x,y);}
static void wg_bind(void) {eg_land_bind(&p,&v,&r,latched,read_global,0);}
static void store(void *ctx,uint8_t x,uint8_t y,uint8_t *b) {
  (void)ctx;cached[y][x/4u]=1;
  for(unsigned k=0;k<4;k++) {full[y][x+k][0]=b[k];full[y][x+k][1]=b[k+4u];full[y][x+k][2]=(uint8_t)(((b[8]>>k)&1u)|(((b[8]>>(4+k))&1u)<<1));}
}
static void load(void *ctx,uint8_t x,uint8_t y,uint8_t *b) {
  (void)ctx;cached[y][x/4u]=1;b[8]=0;
  for(unsigned k=0;k<4;k++) {b[k]=full[y][x+k][0];b[k+4u]=full[y][x+k][1];b[8]|=(uint8_t)(((full[y][x+k][2]&1u)<<k)|(((full[y][x+k][2]>>1)&1u)<<(4+k)));}
}
static uint8_t wg_begin(uint8_t which) {
  if(!lc_plan(&p,which))return 0;
  v.width=p.width;v.height=p.height;latched=0;memset(cached,0,sizeof cached);memset(&r,0,sizeof r);memset(crack,0,sizeof crack);
  unsigned lane=eg_land_lane(&p,2),x=EL_X(&p,2,lane),y=EL_Y(&p,2,lane);
  v.x=x>8 ? (x-8)&~3u : 0;v.y=y>7 ? (y-7)&~3u : 0;
  if(v.x>v.width-16)v.x=v.width-16;if(v.y>v.height-14)v.y=v.height-14;
  for(y=0;y<v.height;y++)for(x=0;x<v.width;x++) {
    eg_land_cell c;eg_land_at(&p,x,y,&c);full[y][x][0]=c.material;full[y][x][1]=skin_of(c.design,c.variant);full[y][x][2]=c.flags&1;
    cached[y][x/4u]=1u; /* the constructor prepares the complete packet store */
  }
  eg_view_load(&v,&r,skin,crack,load,0);r.keys=1;wg_bind();return 1;
}
static void wg_write(uint8_t x,uint8_t y,uint8_t raw) {
  if(x>=v.width || y>=v.height)return;
  unsigned c=local(x,y);
  if(c<224) {r.c[c]=raw;skin[c]=clean(skin[c],raw);}else {cached[y][x/4u]=1;full[y][x][0]=raw;full[y][x][1]=clean(full[y][x][1],raw);}
}
static void wg_source(uint8_t occupied) {
  unsigned channel=0;
  for(unsigned k=0;k<p.nnode;k++) {
    const eg_land_node *n=&p.node[k];if(n->kind!=EL_LOAD && n->kind!=EL_POWER)continue;
    if(n->condition==EL_LATCH) {
      unsigned x=EL_X(&p,n->at-3,n->lane-1),y=EL_Y(&p,n->at-3,n->lane-1),c=local(x,y),raw=wg_read(x,y);
      if(raw==T_BLOCK_PLATE || (raw==T_PLATE && c<224u && c==occupied) || (r.link[channel].gate<224 && (r.c[r.link[channel].gate]&31)==T_GATE_OPEN))latched|=1u<<channel;
    }
    channel++;
  }
}
static void wg_load(uint8_t x,uint8_t y) {cached[y][x/4u]=1;}
static uint8_t wg_shift(int8_t dx,int8_t dy) {
  if(dx) {
    unsigned x=dx>0 ? v.x+16u : v.x-4u;
    for(unsigned y=0;y<14;y++)if(!cached[v.y+y][x/4u])return 0;
  }else {
    unsigned y=dy>0 ? v.y+14u : v.y-4u;
    for(unsigned row=0;row<4;row++)for(unsigned x=0;x<16;x+=4)if(!cached[y+row][(v.x+x)/4u])return 0;
  }
  if(!eg_view_shift(&v,&r,skin,crack,dx,dy,store,load,0))return 0;
  wg_bind();return 1;
}
int main(void) {
  uint8_t out[WG_BYTES];
  for(unsigned which=0;which<LC_CASES;which++) {
    if(!wg_begin(which))return 1;wg_direction=0;
    for(unsigned step=0;step<WG_STEPS;step++) {uint8_t result=wg_step(which,step);wg_capture(result,step,out);if(fwrite(out,1,sizeof out,stdout)!=sizeof out)return 2;}
  }
  return 0;
}
