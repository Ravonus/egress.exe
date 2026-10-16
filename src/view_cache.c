#include <gb/gb.h>
#include <gb/cgb.h>
#include "view_cache.h"
static const uint16_t row_address[54]={
  0,288,576,864,1152,1440,1728,2016,2304,2592,2880,3168,3456,3744,
  4096,4384,4672,4960,5248,5536,5824,6112,6400,6688,6976,7264,7552,7840,
  8192,8480,8768,9056,9344,9632,9920,10208,10496,10784,11072,11360,11648,11936,
  12288,12576,12864,13152,13440,13728,14016,14304,14592,14880,15168,15456
};
void view_put9(uint16_t address,const uint8_t *packet) __sdcccall(0) NONBANKED NAKED {
  address;packet;
  __asm
    push bc
    push de
    ldhl sp,#6
    ld a,(hl+)
    ld e,a
    ld a,(hl+)
    ld b,a
    and a,#15
    or a,#0xD0
    ld d,a
    ld a,b
    swap a
    and a,#15
    add a,#2
    ld b,a
    ld a,(hl+)
    ld h,(hl)
    ld l,a
    ld a,b
    ldh (#0x70),a
    .rept 9
      ld a,(hl+)
      ld (de),a
      inc de
    .endm
    ld a,#1
    ldh (#0x70),a
    pop de
    pop bc
    ret
  __endasm;
}
void view_get9(uint16_t address,uint8_t *packet) __sdcccall(0) NONBANKED NAKED {
  address;packet;
  __asm
    push bc
    push de
    ldhl sp,#6
    ld a,(hl+)
    ld c,a
    ld a,(hl+)
    ld b,a
    swap a
    and a,#15
    add a,#2
    push af
    ld a,(hl+)
    ld e,a
    ld d,(hl)
    ld a,b
    and a,#15
    or a,#0xD0
    ld h,a
    ld l,c
    pop af
    ldh (#0x70),a
    .rept 9
      ld a,(hl+)
      ld (de),a
      inc de
    .endm
    ld a,#1
    ldh (#0x70),a
    pop de
    pop bc
    ret
  __endasm;
}
void view_store(void *ctx,uint8_t x,uint8_t y,uint8_t *packet) NONBANKED {
  uint16_t address;(void)ctx;
  address=(uint16_t)(row_address[y]+(uint8_t)(x>>2)*9u);
  CRITICAL {view_put9(address,packet);}
}
void view_load(void *ctx,uint8_t x,uint8_t y,uint8_t *packet) NONBANKED {
  uint16_t address;(void)ctx;
  address=(uint16_t)(row_address[y]+(uint8_t)(x>>2)*9u);
  CRITICAL {view_get9(address,packet);}
}
uint16_t view_address(uint8_t x,uint8_t y) NONBANKED {return (uint16_t)(row_address[y]+(uint8_t)(x>>2)*9u);}
uint8_t eg_view_shift_horizontal(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,int8_t dx) NONBANKED;
uint8_t eg_view_shift_vertical_prepared(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,int8_t dy) NONBANKED;
extern uint8_t view_prepared;
uint8_t eg_view_shift_cached(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,int8_t dx,int8_t dy) NONBANKED {
  view_prepared=0u;
  if(!eg_view_valid(v) || !r || !skin || !cracked)return 0u;
  if(!dy && (dx==4 || dx==-4))return eg_view_shift_horizontal(v,r,skin,cracked,dx);
  return eg_view_shift(v,r,skin,cracked,dx,dy,view_store,view_load,0);
}
uint8_t eg_view_shift_prepared(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,int8_t dx,int8_t dy) NONBANKED {
  uint8_t result;
  if(!eg_view_valid(v) || !r || !skin || !cracked)return 0u;
  view_prepared=1u;
  if(!dy && (dx==4 || dx==-4))result=eg_view_shift_horizontal(v,r,skin,cracked,dx);
  else if(!dx && (dy==4 || dy==-4))result=eg_view_shift_vertical_prepared(v,r,skin,cracked,dy);
  else result=eg_view_shift(v,r,skin,cracked,dx,dy,0,view_load,0);
  view_prepared=0u;return result;
}
/* All scene moves are a fixed multiple of four (12, 20 or 160 bytes). A
 * bounded four-byte loop avoids libc's general overlap/size path. */
void ev_forward(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED NAKED {
  dst;src;n;
  __asm
    push bc
    push de
    ldhl sp,#6
    ld a,(hl+)
    ld e,a
    ld a,(hl+)
    ld d,a
    ld a,(hl+)
    ld c,a
    ld a,(hl+)
    ld b,a
    ld a,(hl)
    ld h,b
    ld l,c
    rrca
    rrca
    and a,#63
    ld b,a
  1$:
    .rept 4
      ld a,(hl+)
      ld (de),a
      inc de
    .endm
    dec b
    jr nz,1$
    pop de
    pop bc
    ret
  __endasm;
}
void ev_backward(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED NAKED {
  dst;src;n;
  __asm
    push bc
    push de
    ldhl sp,#6
    ld a,(hl+)
    ld e,a
    ld a,(hl+)
    ld d,a
    ld a,(hl+)
    ld c,a
    ld a,(hl+)
    ld b,a
    ld a,(hl)
    push af
    ld h,b
    ld l,c
    dec a
    ld c,a
    ld b,#0
    add hl,bc
    ld a,e
    add a,c
    ld e,a
    jr nc,1$
    inc d
  1$:
    pop af
    rrca
    rrca
    and a,#63
    ld b,a
  2$:
    .rept 4
      ld a,(hl-)
      ld (de),a
      dec de
    .endm
    dec b
    jr nz,2$
    pop de
    pop bc
    ret
  __endasm;
}
