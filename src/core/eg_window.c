#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_window.h"
#define packed eg_window_packet
#ifdef __SDCC
void ew_forward12(void *dst,const void *src) __sdcccall(0) NONBANKED;
void ew_backward12(void *dst,const void *src) __sdcccall(0) NONBANKED;
void ew_pack9(const void *cells,const void *skin,const void *prot,const void *crack,uint8_t from) __sdcccall(0) NONBANKED;
void ew_unpack9(void *cells,void *skin,void *prot,void *crack,uint8_t to) __sdcccall(0) NONBANKED;
#endif
uint8_t eg_window_local(uint8_t origin,uint16_t global) EG_BANKED {
  uint8_t x=(uint8_t)(global&63u),y=(uint8_t)(global>>6);
  if(global>=EW_IDX(0u,EG_H) || x>=EW_WIDTH || x<origin || (uint8_t)(x-origin)>=EG_W)return EG_LINK_NONE;
  return EG_IDX(x-origin,y);
}
uint8_t eg_window_shift(eg_room *r,uint8_t *skin,uint8_t *cracked,uint8_t *origin,int8_t direction,
                       eg_window_io store,eg_window_io load,void *context) EG_BANKED {
  uint8_t row,old,next,from,to;
  uint8_t *cells,*s,*p,*c;
#ifndef __SDCC
  uint8_t pn,cn,*oldc,*olds;
#endif
  if(!r || !skin || !cracked || !origin || !store || !load)return 0u;
  old=*origin;
  if(old>EW_WIDTH-EG_W || (old&(EW_STEP-1u)) || (direction!=1 && direction!=-1))return 0u;
  if((direction==1 && old==EW_WIDTH-EG_W) || (direction==-1 && !old))return 0u;
  next=direction==1 ? (uint8_t)(old+EW_STEP) : (uint8_t)(old-EW_STEP);
  from=direction==1 ? 0u : EG_W-EW_STEP;
  to=direction==1 ? EG_W-EW_STEP : 0u;
  cells=r->c;s=skin;p=r->prot;c=cracked;
  for(row=0u;row<EG_H;row++) {
#ifdef __SDCC
    ew_pack9(cells,s,p,c,from);
#else
    pn=direction==1 ? p[0]&15u : p[1]>>4;
    cn=direction==1 ? c[0]&15u : c[1]>>4;
    oldc=cells+from;olds=s+from;
#define PACK(k) packed[k]=oldc[k];packed[(k)+4u]=olds[k]
    PACK(0);PACK(1);PACK(2);PACK(3);
    packed[8]=(uint8_t)(pn|(cn<<4));
#undef PACK
#endif
    store(context,(uint8_t)(old+from),row,packed);
    load(context,(uint8_t)(next+to),row,packed);
    if(direction==1) {
#ifdef __SDCC
      ew_forward12(cells,cells+EW_STEP);ew_forward12(s,s+EW_STEP);
#else
      memmove(cells,cells+EW_STEP,EG_W-EW_STEP);
      memmove(s,s+EW_STEP,EG_W-EW_STEP);
#endif
    } else {
#ifdef __SDCC
      ew_backward12(cells+EW_STEP,cells);ew_backward12(s+EW_STEP,s);
#else
      memmove(cells+EW_STEP,cells,EG_W-EW_STEP);
      memmove(s+EW_STEP,s,EG_W-EW_STEP);
#endif
    }
#ifdef __SDCC
    ew_unpack9(cells,s,p,c,to);
#else
    oldc=cells+to;olds=s+to;
#define UNPACK(k) oldc[k]=packed[k];olds[k]=packed[(k)+4u]
    UNPACK(0);UNPACK(1);UNPACK(2);UNPACK(3);
#undef UNPACK
    pn=packed[8]&15u;cn=packed[8]>>4;
    if(direction==1) {
      p[0]=(uint8_t)((p[0]>>4)|(p[1]<<4));p[1]=(uint8_t)((p[1]>>4)|(pn<<4));
      c[0]=(uint8_t)((c[0]>>4)|(c[1]<<4));c[1]=(uint8_t)((c[1]>>4)|(cn<<4));
    } else {
      p[1]=(uint8_t)((p[1]<<4)|(p[0]>>4));p[0]=(uint8_t)((p[0]<<4)|pn);
      c[1]=(uint8_t)((c[1]<<4)|(c[0]>>4));c[0]=(uint8_t)((c[0]<<4)|cn);
    }
#endif
    cells+=EG_W;s+=EG_W;p+=2u;c+=2u;
  }
  *origin=next;return 1u;
#undef packed
}
