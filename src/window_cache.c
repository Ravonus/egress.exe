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
