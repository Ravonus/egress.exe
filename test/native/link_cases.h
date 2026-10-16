#ifndef EG_LINK_CASES_H
#define EG_LINK_CASES_H
#include <string.h>
#include "core/eg_room.h"
#define LC_CASES 16u
#define LC_STEPS 32u
#define LC_BYTES 376u
static void lc_begin(eg_room *r,uint8_t k) {
  uint8_t i;
  memset(r,0,sizeof *r);for(i=0u;i!=EG_N;i++)r->c[i]=T_WALL;
  r->nlink=(uint8_t)(1u+(k&1u));
  r->link[0].a=84u;r->link[0].b=k&2u ? EG_LINK_NONE : 86u;
  r->link[0].gate=68u;r->link[0].rule=(uint8_t)((k>>2)&3u);
  r->link[1].a=EG_LINK_EXTERNAL;r->link[1].b=EG_LINK_NONE;r->link[1].gate=70u;
  r->link[1].rule=k&8u ? EG_LINK_LATCH : 0u;
  r->c[68u]=r->c[70u]=T_GATE;
  r->portal[0]=k&8u ? 149u : 116u;r->portal[1]=k&8u ? 116u : 149u;
  r->portal_need=(uint8_t)(EG_PORTAL_ON|(k&3u));
}
static void lc_step(eg_room *r,uint8_t k,uint8_t step,uint8_t *out) {
  static const uint8_t weights[4]={T_PLATE,T_BLOCK_PLATE,T_ROCK|F_PLATE,T_PLATE|F_OB};
  uint8_t occ[2],n=0u,info[8],i,p=r->portal[0],q=r->portal[1],above=(uint8_t)(p-EG_W);
  r->c[84u]=weights[step&3u];r->c[86u]=step&4u ? T_SOCKET_ON : T_SOCKET;
  r->link[1].rule=(uint8_t)((r->link[1].rule&EG_LINK_LATCH)|(step&8u ? EG_LINK_EXT_A : 0u));
  if(step%5u==0u)occ[n++]=84u;
  if(step%7u==0u)occ[n++]=68u;
  if(step%9u==0u) {n=1u;occ[0]=q;}
  r->c[above]=T_WALL;
  r->c[p]=step&1u ? T_BLOCK_PLATE : T_ROCK|F_PLATE;
  r->c[q]=step&2u ? T_PLATE : T_EMPTY;
  if(step%11u==0u)r->c[q]=T_WALL;
  memset(eg_dirty,0,EG_DIRTY_MAX);eg_ndirty=0u;eg_plates_dirty=1u;
  info[0]=eg_shutters(r,occ,n);
  info[1]=eg_link_power(r,0u,occ,n);info[2]=eg_link_power(r,1u,occ,n);
  info[3]=eg_portal_target(r,p,occ,n);
  info[4]=eg_portal_object(r,p,occ,n);
  if((step&7u)==7u) {r->c[above]=T_ROCK;r->c[p]=r->c[q]=T_EMPTY;}
  info[5]=eg_gravity(r,occ,n);info[6]=eg_ndirty;info[7]=(uint8_t)(k^step);
  memcpy(out,r,sizeof *r);
  for(i=0u;i<EG_DIRTY_MAX;i++)out[320u+i]=eg_dirty[i];
  memcpy(out+368u,info,sizeof info);
}
#endif
