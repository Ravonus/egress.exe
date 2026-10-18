/* Native 48 by 14 backing grid: twelve nine-byte strips per row in WRAM bank 4.
 * Only the bounded cache leaves mask interrupts; the full shift stays interruptible. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "core/eg_window.h"
typedef char native_window_cache_layout[(EW_WIDTH==48u && EG_H==14u) ? 1 : -1];
static uint8_t *ew_cells,*ew_skin,*ew_prot,*ew_crack;
static uint16_t ew_out,ew_in;
static uint8_t ew_from,ew_to,ew_rows;
static void kernel(void) NONBANKED NAKED {
__asm
.globl _ew_pack9,_ew_unpack9,_ew_forward12,_ew_backward12,_ew_store9,_ew_load9
push bc
push de
1$:
ld a,(#_ew_from)
push af
inc sp
ld hl,#_ew_crack
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#_ew_prot
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
push de
call _ew_pack9
add sp,#9
ld hl,#_eg_window_packet
push hl
ld hl,#_ew_out
ld a,(hl+)
ld d,(hl)
ld e,a
push de
di
call _ew_store9
add sp,#4
ei
ld hl,#_eg_window_packet
push hl
ld hl,#_ew_in
ld a,(hl+)
ld d,(hl)
ld e,a
push de
di
call _ew_load9
add sp,#4
ei
ld a,(#_ew_from)
or a,a
jr nz,2$
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#4
add hl,de
push hl
push de
call _ew_forward12
add sp,#4
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#4
add hl,de
push hl
push de
call _ew_forward12
add sp,#4
jr 3$
2$:
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#4
add hl,de
push hl
call _ew_backward12
add sp,#4
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#4
add hl,de
push hl
call _ew_backward12
add sp,#4
3$:
ld a,(#_ew_to)
push af
inc sp
ld hl,#_ew_crack
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#_ew_prot
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
push de
call _ew_unpack9
add sp,#9
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#16
add hl,de
ld a,l
ld (#_ew_cells),a
ld a,h
ld (#_ew_cells+1),a
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#16
add hl,de
ld a,l
ld (#_ew_skin),a
ld a,h
ld (#_ew_skin+1),a
ld hl,#_ew_prot
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#2
add hl,de
ld a,l
ld (#_ew_prot),a
ld a,h
ld (#_ew_prot+1),a
ld hl,#_ew_crack
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#2
add hl,de
ld a,l
ld (#_ew_crack),a
ld a,h
ld (#_ew_crack+1),a
ld hl,#_ew_out
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#108
add hl,de
ld a,l
ld (#_ew_out),a
ld a,h
ld (#_ew_out+1),a
ld hl,#_ew_in
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#108
add hl,de
ld a,l
ld (#_ew_in),a
ld a,h
ld (#_ew_in+1),a
ld hl,#_ew_rows
dec (hl)
jp nz,1$
pop de
pop bc
ret
__endasm;
}
uint8_t eg_window_shift_cached(eg_room *r,uint8_t *skin,uint8_t *cracked,uint8_t *origin,int8_t direction) NONBANKED {
 uint8_t old,next;
 if(!r || !skin || !cracked || !origin)return 0u;
 old=*origin;
 if(old>EW_WIDTH-EG_W || (old&3u) || (direction!=1 && direction!=-1))return 0u;
 if((direction==1 && old==EW_WIDTH-EG_W) || (direction==-1 && !old))return 0u;
 next=direction==1 ? old+4u : old-4u;
 ew_from=direction==1 ? 0u : 12u;ew_to=direction==1 ? 12u : 0u;
 ew_cells=r->c;ew_skin=skin;ew_prot=r->prot;ew_crack=cracked;ew_rows=14u;
 ew_out=(uint8_t)(((old+ew_from)>>2)*9u);ew_in=(uint8_t)(((next+ew_to)>>2)*9u);
 kernel();*origin=next;return 1u;
}

#include "view_cache.h"
uint8_t view_prepared;
static uint8_t ew_bank_rows;
static void view_kernel(void) NONBANKED NAKED {
__asm
.globl _ew_pack9,_ew_unpack9,_ew_forward12,_ew_backward12,_view_put9,_view_get9
push bc
push de
1$:
ld a,(#_view_prepared)
or a,a
jr nz,5$
ld a,(#_ew_from)
push af
inc sp
ld hl,#_ew_crack
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#_ew_prot
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
push de
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
push de
call _ew_pack9
add sp,#9
ld hl,#_eg_window_packet
push hl
ld hl,#_ew_out
ld a,(hl+)
ld d,(hl)
ld e,a
push de
di
call _view_put9
add sp,#4
ei
5$:
ld hl,#_eg_window_packet
push hl
ld hl,#_ew_in
ld a,(hl+)
ld d,(hl)
ld e,a
push de
di
call _view_get9
add sp,#4
ei
ld a,(#_ew_from)
or a,a
jr nz,2$
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#4
add hl,de
.rept 12
ld a,(hl+)
ld (de),a
inc de
.endm
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#4
add hl,de
.rept 12
ld a,(hl+)
ld (de),a
inc de
.endm
jr 3$
2$:
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#11
add hl,de
ld a,e
add a,#15
ld e,a
jr nc,6$
inc d
6$:
.rept 12
ld a,(hl-)
ld (de),a
dec de
.endm
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#11
add hl,de
ld a,e
add a,#15
ld e,a
jr nc,7$
inc d
7$:
.rept 12
ld a,(hl-)
ld (de),a
dec de
.endm
3$:
ld a,(#_ew_to)
ld c,a
ld hl,#_ew_cells
ld a,(hl+)
add a,c
ld e,a
ld a,(hl)
adc a,#0
ld d,a
ld hl,#_eg_window_packet+0
.rept 4
ld a,(hl+)
ld (de),a
inc de
.endm
ld hl,#_ew_skin
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
ld a,c
or a,a
jr z,8$
ld hl,#_ew_prot
ld a,(hl+)
ld h,(hl)
ld l,a
ld d,(hl)
inc hl
ld e,(hl)
ld a,d
swap a
and a,#15
ld b,a
ld a,e
swap a
and a,#240
or a,b
dec hl
ld (hl+),a
ld a,e
swap a
and a,#15
ld b,a
ld a,(#_eg_window_packet+8)
swap a
and a,#240
or a,b
ld (hl),a
ld hl,#_ew_crack
ld a,(hl+)
ld h,(hl)
ld l,a
ld d,(hl)
inc hl
ld e,(hl)
ld a,d
swap a
and a,#15
ld b,a
ld a,e
swap a
and a,#240
or a,b
dec hl
ld (hl+),a
ld a,e
swap a
and a,#15
ld b,a
ld a,(#_eg_window_packet+8)
and a,#240
or a,b
ld (hl),a
jp 9$
8$:
ld hl,#_ew_prot
ld a,(hl+)
ld h,(hl)
ld l,a
ld d,(hl)
inc hl
ld e,(hl)
ld a,d
swap a
and a,#15
ld b,a
ld a,e
swap a
and a,#240
or a,b
ld (hl-),a
ld a,d
swap a
and a,#240
ld b,a
ld a,(#_eg_window_packet+8)
and a,#15
or a,b
ld (hl),a
ld hl,#_ew_crack
ld a,(hl+)
ld h,(hl)
ld l,a
ld d,(hl)
inc hl
ld e,(hl)
ld a,d
swap a
and a,#15
ld b,a
ld a,e
swap a
and a,#240
or a,b
ld (hl-),a
ld a,d
swap a
and a,#240
ld b,a
ld a,(#_eg_window_packet+8)
swap a
and a,#15
or a,b
ld (hl),a
9$:
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#16
add hl,de
ld a,l
ld (#_ew_cells),a
ld a,h
ld (#_ew_cells+1),a
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#16
add hl,de
ld a,l
ld (#_ew_skin),a
ld a,h
ld (#_ew_skin+1),a
ld hl,#_ew_prot
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#2
add hl,de
ld a,l
ld (#_ew_prot),a
ld a,h
ld (#_ew_prot+1),a
ld hl,#_ew_crack
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#2
add hl,de
ld a,l
ld (#_ew_crack),a
ld a,h
ld (#_ew_crack+1),a
ld a,(#_view_prepared)
or a,a
jr nz,10$
ld hl,#_ew_out
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#288
add hl,de
ld a,l
ld (#_ew_out),a
ld a,h
ld (#_ew_out+1),a
10$:
ld hl,#_ew_in
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#288
add hl,de
ld a,l
ld (#_ew_in),a
ld a,h
ld (#_ew_in+1),a
ld hl,#_ew_bank_rows
dec (hl)
jr nz,4$
ld (hl),#14
ld hl,#_ew_out
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#64
add hl,de
ld a,l
ld (#_ew_out),a
ld a,h
ld (#_ew_out+1),a
ld hl,#_ew_in
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#64
add hl,de
ld a,l
ld (#_ew_in),a
ld a,h
ld (#_ew_in+1),a
4$:
ld hl,#_ew_rows
dec (hl)
jp nz,1$
pop de
pop bc
ret
__endasm;
}

/* Already-preserved seams can move the overlap as two contiguous spans.
 * Cross-row spill is overwritten by the incoming four-cell packets. Only
 * this leaf installs them; it shares the full-store path's bank addresses. */
static void prepared_kernel(void) NONBANKED NAKED {
__asm
push bc
push de
1$:
ld hl,#_eg_window_packet
push hl
ld hl,#_ew_in
ld a,(hl+)
ld d,(hl)
ld e,a
push de
di
call _view_get9
add sp,#4
ei
ld hl,#_ew_cells
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#_eg_window_packet
.rept 4
ld a,(hl+)
ld (de),a
inc de
.endm
ld hl,#_ew_skin
ld a,(hl+)
ld d,(hl)
ld e,a
ld hl,#_eg_window_packet+4
.rept 4
ld a,(hl+)
ld (de),a
inc de
.endm
ld a,(#_eg_window_packet+8)
ld b,a
swap a
and a,#15
ld c,a
ld a,b
and a,#15
ld b,a
ld a,(#_ew_to)
or a,a
jr z,2$
swap b
swap c
2$:
ld hl,#_ew_prot
ld a,(hl+)
ld h,(hl)
ld l,a
ld a,b
or a,(hl)
ld (hl),a
ld hl,#_ew_crack
ld a,(hl+)
ld h,(hl)
ld l,a
ld a,c
or a,(hl)
ld (hl),a
ld a,(#_ew_from)
cp a,#4
ld b,#16
jr nz,3$
ld b,#4
3$:
ld hl,#_ew_cells
ld a,(hl)
add a,b
ld (hl+),a
jr nc,4$
inc (hl)
4$:
ld hl,#_ew_skin
ld a,(hl)
add a,b
ld (hl+),a
jr nc,5$
inc (hl)
5$:
ld a,(#_ew_from)
cp a,#4
ld b,#2
jr nz,10$
ld a,(#_ew_to)
or a,a
jr z,7$
ld b,#1
10$:
ld hl,#_ew_prot
ld a,(hl)
add a,b
ld (hl+),a
jr nc,6$
inc (hl)
6$:
ld hl,#_ew_crack
ld a,(hl)
add a,b
ld (hl+),a
jr nc,7$
inc (hl)
7$:
ld a,(#_ew_from)
cp a,#4
jr nz,11$
ld a,(#_ew_to)
xor a,#4
ld (#_ew_to),a
ld hl,#_ew_in
ld a,(hl)
add a,#9
ld (hl+),a
jr nc,12$
inc (hl)
12$:
ld hl,#_ew_out
dec (hl)
jr nz,8$
ld (hl),#4
ld de,#252
jr 13$
11$:
ld de,#288
13$:
ld hl,#_ew_in
ld a,(hl+)
ld h,(hl)
ld l,a
add hl,de
ld a,l
ld (#_ew_in),a
ld a,h
ld (#_ew_in+1),a
ld hl,#_ew_bank_rows
dec (hl)
jr nz,8$
ld (hl),#14
ld hl,#_ew_in
ld a,(hl)
add a,#64
ld (hl+),a
jr nc,8$
inc (hl)
8$:
ld hl,#_ew_rows
dec (hl)
jp nz,1$
pop de
pop bc
ret
__endasm;
}
void ev_forward(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED;
void ev_backward(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED;
/* World packet rows are 0..53. Two bounded subtractions replace the general
 * 8/16-bit remainder helpers in the atomic camera transfer. */
static uint8_t bank_rows_left(uint8_t row) {
 if(row>=28u)row-=28u;
 if(row>=14u)row-=14u;
 return (uint8_t)(14u-row);
}
uint8_t eg_view_shift_horizontal(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,int8_t dx) NONBANKED {
 uint8_t old=v->x,next;
 if((dx<0 && !old) || (dx>0 && old==v->width-EG_W))return 0u;
 next=(uint8_t)(old+dx);
 ew_from=dx>0 ? 0u : 12u;ew_to=dx>0 ? 12u : 0u;
 ew_cells=r->c;ew_skin=skin;ew_prot=r->prot;ew_crack=cracked;ew_rows=14u;
 ew_bank_rows=bank_rows_left(v->y);
 ew_out=view_address((uint8_t)(old+ew_from),v->y);ew_in=view_address((uint8_t)(next+ew_to),v->y);
 if(view_prepared) {
   if(dx>0) {ev_forward(r->c,r->c+4u,220u);ev_forward(skin,skin+4u,220u);}
   else {ev_backward(r->c+4u,r->c,220u);ev_backward(skin+4u,skin,220u);}
   eg_view_bits(r->prot,dx,0,0u);eg_view_bits(cracked,dx,0,0u);
   ew_cells+=ew_to;ew_skin+=ew_to;ew_prot+=ew_to>>3;ew_crack+=ew_to>>3;
   prepared_kernel();
 } else view_kernel();
 v->x=next;return 1u;
}
uint8_t eg_view_shift_vertical_prepared(eg_view *v,eg_room *r,uint8_t *skin,uint8_t *cracked,int8_t dy) NONBANKED {
 uint8_t next,from;
 if((dy<0 && !v->y) || (dy>0 && v->y==v->height-EG_H))return 0u;
 next=(uint8_t)(v->y+dy);from=dy>0 ? 10u : 0u;
 if(dy>0) {ev_forward(r->c,r->c+64u,160u);ev_forward(skin,skin+64u,160u);}
 else {ev_backward(r->c+64u,r->c,160u);ev_backward(skin+64u,skin,160u);}
 eg_view_bits(r->prot,0,dy,0u);eg_view_bits(cracked,0,dy,0u);
 ew_cells=r->c+(uint8_t)(from<<4);ew_skin=skin+(uint8_t)(from<<4);
 ew_prot=r->prot+(uint8_t)(from<<1);ew_crack=cracked+(uint8_t)(from<<1);
 ew_from=4u;ew_to=0u;ew_out=4u;ew_rows=16u;
 ew_in=view_address(v->x,(uint8_t)(next+from));ew_bank_rows=bank_rows_left((uint8_t)(next+from));
 prepared_kernel();v->y=next;return 1u;
}
