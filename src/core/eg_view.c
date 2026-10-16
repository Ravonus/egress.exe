#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_view.h"
#ifdef __SDCC
void ev_forward(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED;
void ev_backward(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED;
#else
#define ev_forward(d,s,n) memmove(d,s,n)
#define ev_backward(d,s,n) memmove(d,s,n)
#endif
static uint8_t valid(const eg_view *v) {
  return v && v->width>=EG_W && v->width<=128u && !(v->width&3u) &&
    v->height>=EG_H && v->height<=54u && !((v->height-EG_H)&3u) &&
    v->x<=v->width-EG_W && v->y<=v->height-EG_H && !((v->x|v->y)&3u);
}
uint8_t eg_view_valid(const eg_view *v) EG_BANKED {return valid(v);}
uint8_t eg_view_rebase(uint8_t cell,int8_t dx,int8_t dy) EG_RNG_ABI {
  uint8_t x,y;
  if(cell>=EG_N)return EG_LINK_NONE;
  x=(uint8_t)(EG_X(cell)-dx);y=(uint8_t)(EG_Y(cell)-dy);
  if(x>=EG_W || y>=EG_H)return EG_LINK_NONE;
  return EG_IDX(x,y);
}
#ifdef __SDCC
/* Register ABI: DE=bitmap, A=dx, dy/fill are two callee-cleaned stack bytes.
 * The independent portable implementation below supplies the bit oracle. */
void eg_view_bits(uint8_t *bits,int8_t dx,int8_t dy,uint8_t fill) NONBANKED NAKED {
  bits;dx;dy;fill;
  __asm
    push bc
    push de
    ld c,a
    ldhl sp,#6
    ld b,(hl)
    or a,a
    jr z,4$
    ld a,b
    or a,a
    jp nz,9$
    ld a,c
    cp a,#4
    jr z,1$
    cp a,#252
    jp nz,9$
    ldhl sp,#7
    ld a,(hl)
    or a,a
    ld c,#0
    jr z,2$
    ld c,#15
  2$:
    ldhl sp,#0
    ld a,(hl+)
    ld h,(hl)
    ld l,a
    ld b,#14
  3$:
    ld a,(hl+)
    swap a
    ld d,a
    and a,#15
    ld e,a
    ld a,(hl)
    swap a
    and a,#240
    or a,e
    ld (hl-),a
    ld a,d
    and a,#240
    or a,c
    ld (hl+),a
    inc hl
    dec b
    jr nz,3$
    jp 9$
  1$:
    ldhl sp,#7
    ld a,(hl)
    or a,a
    ld c,#0
    jr z,10$
    ld c,#240
  10$:
    ldhl sp,#0
    ld a,(hl+)
    ld h,(hl)
    ld l,a
    ld b,#14
  11$:
    ld a,(hl+)
    swap a
    and a,#15
    ld e,a
    ld a,(hl)
    swap a
    ld d,a
    and a,#240
    or a,e
    dec hl
    ld (hl+),a
    ld a,d
    and a,#15
    or a,c
    ld (hl+),a
    dec b
    jr nz,11$
    jp 9$
  4$:
    ld a,b
    cp a,#4
    jr z,5$
    cp a,#252
    jp nz,9$
    ldhl sp,#7
    ld a,(hl)
    or a,a
    ld c,#0
    jr z,6$
    dec c
  6$:
    ldhl sp,#0
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    ld hl,#19
    add hl,de
    ld a,e
    add a,#27
    ld e,a
    jr nc,12$
    inc d
  12$:
    ld b,#20
  13$:
    ld a,(hl-)
    ld (de),a
    dec de
    dec b
    jr nz,13$
    ld b,#8
  14$:
    ld a,c
    ld (de),a
    dec de
    dec b
    jr nz,14$
    jr 9$
  5$:
    ldhl sp,#7
    ld a,(hl)
    or a,a
    ld c,#0
    jr z,7$
    dec c
  7$:
    ldhl sp,#0
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    ld hl,#8
    add hl,de
    ld b,#20
  8$:
    ld a,(hl+)
    ld (de),a
    inc de
    dec b
    jr nz,8$
    ld b,#8
  15$:
    ld a,c
    ld (de),a
    inc de
    dec b
    jr nz,15$
  9$:
    pop de
    pop bc
    pop hl
    add sp,#2
    jp (hl)
  __endasm;
}
#else
void eg_view_bits(uint8_t *bits,int8_t dx,int8_t dy,uint8_t fill) {
  uint8_t k,a,b,f=fill ? 255u : 0u;
  if((dx && dy) || (!dx && !dy) || (dx && dx!=4 && dx!=-4) || (dy && dy!=4 && dy!=-4))return;
  if(dy>0) {ev_forward(bits,bits+8u,20u);memset(bits+20u,f,8u);return;}
  if(dy<0) {ev_backward(bits+8u,bits,20u);memset(bits,f,8u);return;}
  for(k=0u;k<14u;k++) {
    a=bits[0];b=bits[1];
    if(dx>0) {bits[0]=(uint8_t)((a>>4)|(b<<4));bits[1]=(uint8_t)((b>>4)|(f&240u));}
    else {bits[0]=(uint8_t)((a<<4)|(f&15u));bits[1]=(uint8_t)((b<<4)|(a>>4));}
    bits+=2u;
  }
}
#endif
uint8_t eg_view_local(const eg_view *v,uint8_t x,uint8_t y) EG_BANKED {
  if(x<v->x || y<v->y || (uint8_t)(x-v->x)>=EG_W || (uint8_t)(y-v->y)>=EG_H)return EG_LINK_NONE;
  return EG_IDX(x-v->x,y-v->y);
}
#ifdef __SDCC
/* Register ABI: room in DE, skin in BC; crack pointer, x and y on stack.
 * These four-cell leaves keep the portable implementation below as the host
 * reference. They do not change WRAM banks or mask interrupts. */
static void pack(const eg_room *r,const uint8_t *s,const uint8_t *c,uint8_t x,uint8_t y) NONBANKED NAKED {
  r;s;c;x;y;
  __asm
    push bc
    push de
    ldhl sp,#9
    ld a,(hl-)
    swap a
    and a,#0xf0
    add a,(hl)
    ld c,a
    ld a,e
    add a,c
    ld e,a
    jr nc,1$
    inc d
  1$:
    ld hl,#_eg_window_packet
    .rept 4
      ld a,(de)
      inc de
      ld (hl+),a
    .endm
    ldhl sp,#2
    ld a,(hl+)
    add a,c
    ld e,a
    ld a,(hl)
    adc a,#0
    ld d,a
    ld hl,#_eg_window_packet+4
    .rept 4
      ld a,(de)
      inc de
      ld (hl+),a
    .endm
    ldhl sp,#0
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    ld a,c
    rrca
    rrca
    rrca
    and a,#31
    add a,#2
    ld l,a
    ld h,#1
    add hl,de
    ld b,(hl)
    ldhl sp,#8
    bit 2,(hl)
    jr z,2$
    swap b
  2$:
    ld a,b
    and a,#15
    ld b,a
    ldhl sp,#6
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    ld a,c
    rrca
    rrca
    rrca
    and a,#31
    ld l,a
    ld h,#0
    add hl,de
    ld d,(hl)
    ldhl sp,#8
    bit 2,(hl)
    jr nz,3$
    swap d
  3$:
    ld a,d
    and a,#0xf0
    or a,b
    ld (#_eg_window_packet+8),a
    pop de
    pop bc
    pop hl
    add sp,#4
    jp (hl)
  __endasm;
}
static void unpack(eg_room *r,uint8_t *s,uint8_t *c,uint8_t x,uint8_t y) NONBANKED NAKED {
  r;s;c;x;y;
  __asm
    push bc
    push de
    ldhl sp,#9
    ld a,(hl-)
    swap a
    and a,#0xf0
    add a,(hl)
    ld c,a
    ld a,(hl)
    and a,#4
    ld b,a
    ld a,c
    rrca
    rrca
    rrca
    and a,#31
    add a,#2
    ld l,a
    ld h,#1
    add hl,de
    ld a,b
    or a,a
    jr z,1$
    ld a,(hl)
    and a,#15
    ld d,a
    ld a,(#_eg_window_packet+8)
    swap a
    and a,#0xf0
    jr 2$
  1$:
    ld a,(hl)
    and a,#0xf0
    ld d,a
    ld a,(#_eg_window_packet+8)
    and a,#15
  2$:
    or a,d
    ld (hl),a
    ldhl sp,#6
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    ld a,c
    rrca
    rrca
    rrca
    and a,#31
    ld l,a
    ld h,#0
    add hl,de
    ld a,b
    or a,a
    jr z,3$
    ld a,(hl)
    and a,#15
    ld d,a
    ld a,(#_eg_window_packet+8)
    and a,#0xf0
    jr 4$
  3$:
    ld a,(hl)
    and a,#0xf0
    ld d,a
    ld a,(#_eg_window_packet+8)
    swap a
    and a,#15
  4$:
    or a,d
    ld (hl),a
    ldhl sp,#0
    ld a,(hl+)
    add a,c
    ld e,a
    ld a,(hl)
    adc a,#0
    ld d,a
    ld hl,#_eg_window_packet
    .rept 4
      ld a,(hl+)
      ld (de),a
      inc de
    .endm
    ldhl sp,#2
    ld a,(hl+)
    add a,c
    ld e,a
    ld a,(hl)
    adc a,#0
    ld d,a
    ld hl,#_eg_window_packet+4
    .rept 4
      ld a,(hl+)
      ld (de),a
      inc de
    .endm
    pop de
    pop bc
    pop hl
    add sp,#4
    jp (hl)
  __endasm;
}
#else
static void pack(const eg_room *r,const uint8_t *s,const uint8_t *c,uint8_t x,uint8_t y) {
  uint8_t i=EG_IDX(x,y),j=(uint8_t)(i>>3),p=r->prot[j],cr=c[j];
  const uint8_t *cell=r->c+i,*skin=s+i;
  /* Read flags before any copy call: SDCC otherwise loses the protection
   * pointer while spilling two memcpy arguments. Native traces cover this. */
  if(x&4u) {p>>=4;cr>>=4;}
  eg_window_packet[8]=(uint8_t)((p&15u)|(cr<<4));
#define COPY(k) eg_window_packet[k]=cell[k];eg_window_packet[(k)+4u]=skin[k]
  COPY(0);COPY(1);COPY(2);COPY(3);
#undef COPY
}
static void unpack(eg_room *r,uint8_t *s,uint8_t *c,uint8_t x,uint8_t y) {
  uint8_t i=EG_IDX(x,y),j=(uint8_t)(i>>3),p=eg_window_packet[8]&15u,cr=eg_window_packet[8]>>4,keep=240u;
  uint8_t *cell=r->c+i,*skin=s+i;
  if(x&4u) {keep=15u;p<<=4;cr<<=4;}
  r->prot[j]=(uint8_t)((r->prot[j]&keep)|p);c[j]=(uint8_t)((c[j]&keep)|cr);
#define COPY(k) cell[k]=eg_window_packet[k];skin[k]=eg_window_packet[(k)+4u]
  COPY(0);COPY(1);COPY(2);COPY(3);
#undef COPY
}
#endif
uint8_t eg_view_load(const eg_view *v,eg_room *r,uint8_t *s,uint8_t *c,eg_view_io load,void *context) EG_BANKED {
  uint8_t x,y;if(!valid(v) || !r || !s || !c || !load)return 0u;
  for(y=0u;y<EG_H;y++)for(x=0u;x<EG_W;x+=4u) {load(context,v->x+x,v->y+y,eg_window_packet);unpack(r,s,c,x,y);}
  return 1u;
}
uint8_t eg_view_flush(const eg_view *v,const eg_room *r,const uint8_t *s,const uint8_t *c,eg_view_io store,void *context) EG_BANKED {
  uint8_t x,y;if(!valid(v) || !r || !s || !c || !store)return 0u;
  for(y=0u;y<EG_H;y++)for(x=0u;x<EG_W;x+=4u) {pack(r,s,c,x,y);store(context,v->x+x,v->y+y,eg_window_packet);}
  return 1u;
}
uint8_t eg_view_shift(eg_view *v,eg_room *r,uint8_t *s,uint8_t *c,int8_t dx,int8_t dy,
                      eg_view_io store,eg_view_io load,void *context) EG_BANKED {
  uint8_t x,y,oldx,oldy,from,to,pi,ci;
  if(!valid(v) || !r || !s || !c || !load)return 0u;
  if((dx && dy) || (!dx && !dy) || (dx && dx!=4 && dx!=-4) || (dy && dy!=4 && dy!=-4))return 0u;
  if((dx<0 && !v->x) || (dy<0 && !v->y) || (dx>0 && v->x==v->width-EG_W) || (dy>0 && v->y==v->height-EG_H))return 0u;
  oldx=v->x;oldy=v->y;
  if(dx) {
    from=dx>0 ? 0u : 12u;to=dx>0 ? 12u : 0u;
    for(y=0u;y<EG_H;y++) {
      if(store) {pack(r,s,c,from,y);store(context,oldx+from,oldy+y,eg_window_packet);}
      pi=(uint8_t)(y<<1);ci=EG_IDX(0u,y);
      if(dx>0) {
        ev_forward(r->c+ci,r->c+ci+4u,12u);ev_forward(s+ci,s+ci+4u,12u);
        r->prot[pi]=(uint8_t)((r->prot[pi]>>4)|(r->prot[pi+1u]<<4));r->prot[pi+1u]>>=4;
        c[pi]=(uint8_t)((c[pi]>>4)|(c[pi+1u]<<4));c[pi+1u]>>=4;
      } else {
        ev_backward(r->c+ci+4u,r->c+ci,12u);ev_backward(s+ci+4u,s+ci,12u);
        r->prot[pi+1u]=(uint8_t)((r->prot[pi+1u]<<4)|(r->prot[pi]>>4));r->prot[pi]<<=4;
        c[pi+1u]=(uint8_t)((c[pi+1u]<<4)|(c[pi]>>4));c[pi]<<=4;
      }
      load(context,(uint8_t)(oldx+dx+to),oldy+y,eg_window_packet);unpack(r,s,c,to,y);
    }
  } else {
    from=dy>0 ? 0u : 10u;to=dy>0 ? 10u : 0u;
    if(store)for(y=from;y<from+4u;y++)for(x=0u;x<EG_W;x+=4u) {pack(r,s,c,x,y);store(context,oldx+x,oldy+y,eg_window_packet);}
    if(dy>0) {
      ev_forward(r->c,r->c+64u,160u);ev_forward(s,s+64u,160u);
      ev_forward(r->prot,r->prot+8u,20u);ev_forward(c,c+8u,20u);
    } else {
      ev_backward(r->c+64u,r->c,160u);ev_backward(s+64u,s,160u);
      ev_backward(r->prot+8u,r->prot,20u);ev_backward(c+8u,c,20u);
    }
    for(y=to;y<to+4u;y++)for(x=0u;x<EG_W;x+=4u) {load(context,oldx+x,(uint8_t)(oldy+dy+y),eg_window_packet);unpack(r,s,c,x,y);}
  }
  v->x=(uint8_t)(oldx+dx);v->y=(uint8_t)(oldy+dy);return 1u;
}
