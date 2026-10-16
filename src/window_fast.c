#include <gb/gb.h>
#include <gb/cgb.h>
#include "core/eg_window.h"
typedef char native_window_row_size[(EG_W==16u && EW_STEP==4u) ? 1 : -1];
void ew_pack9(const void *cells,const void *skin,const void *prot,const void *crack,uint8_t from) __sdcccall(0) NONBANKED NAKED {
 cells;skin;prot;crack;from;
 __asm
  push bc
  push de
  ldhl sp,#14
  ld c,(hl)
  ldhl sp,#6
  ld a,(hl+)
  add a,c
  ld e,a
  ld a,(hl)
  adc a,#0
  ld d,a
  ld hl,#_eg_window_packet
  .rept 4
   ld a,(de)
   inc de
   ld (hl+),a
  .endm
  ldhl sp,#8
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
  ldhl sp,#10
  ld a,(hl+)
  ld d,(hl)
  ld e,a
  ld a,c
  or a,a
  jr nz,1$
  ld a,(de)
  and a,#15
  jr 2$
 1$:
  inc de
  ld a,(de)
  swap a
  and a,#15
 2$:
  ld b,a
  ldhl sp,#12
  ld a,(hl+)
  ld d,(hl)
  ld e,a
  ld a,c
  or a,a
  jr nz,3$
  ld a,(de)
  swap a
  and a,#0xf0
  jr 4$
 3$:
  inc de
  ld a,(de)
  and a,#0xf0
 4$:
  or a,b
  ld (#_eg_window_packet+8),a
  pop de
  pop bc
  ret
 __endasm;
}
void ew_unpack9(void *cells,void *skin,void *prot,void *crack,uint8_t to) __sdcccall(0) NONBANKED NAKED {
 cells;skin;prot;crack;to;
 __asm
  push bc
  push de
  ldhl sp,#14
  ld c,(hl)
  ldhl sp,#6
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
  ldhl sp,#8
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
  /* Preserve the destination nibble offset across both flag rows. */
  ld a,c
  or a,a
  jr z,5$
  /* Moving east: the low nibble leaves, the new nibble enters at bit 12. */
  ldhl sp,#10
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
  and a,#0xf0
  or a,b
  dec hl
  ld (hl+),a
  ld a,e
  swap a
  and a,#15
  ld b,a
  ld a,(#_eg_window_packet+8)
  swap a
  and a,#0xf0
  or a,b
  ld (hl),a
  ldhl sp,#12
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
  and a,#0xf0
  or a,b
  dec hl
  ld (hl+),a
  ld a,e
  swap a
  and a,#15
  ld b,a
  ld a,(#_eg_window_packet+8)
  and a,#0xf0
  or a,b
  ld (hl),a
  jr 6$
 5$:
  /* Moving west: the high nibble leaves; incoming flags fill bits 0..3. */
  ldhl sp,#10
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
  and a,#0xf0
  or a,b
  ld (hl-),a
  ld a,d
  swap a
  and a,#0xf0
  ld b,a
  ld a,(#_eg_window_packet+8)
  and a,#15
  or a,b
  ld (hl),a
  ldhl sp,#12
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
  and a,#0xf0
  or a,b
  ld (hl-),a
  ld a,d
  swap a
  and a,#0xf0
  ld b,a
  ld a,(#_eg_window_packet+8)
  swap a
  and a,#15
  or a,b
  ld (hl),a
 6$:
  pop de
  pop bc
  ret
 __endasm;
}
/* The fixed-size row moves do not need the generic libc overlap loop. */
void ew_forward12(void *dst,const void *src) __sdcccall(0) NONBANKED NAKED {
 dst;src;
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
  ld b,(hl)
  ld h,b
  ld l,c
  .rept 12
   ld a,(hl+)
   ld (de),a
   inc de
  .endm
  pop de
  pop bc
  ret
 __endasm;
}
void ew_backward12(void *dst,const void *src) __sdcccall(0) NONBANKED NAKED {
 dst;src;
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
  ld b,(hl)
  ld h,b
  ld l,c
  ld bc,#11
  add hl,bc
  ld a,e
  add a,#11
  ld e,a
  jr nc,1$
  inc d
 1$:
  .rept 12
   ld a,(hl-)
   ld (de),a
   dec de
  .endm
  pop de
  pop bc
  ret
 __endasm;
}
/* The packet lives in fixed C000..CFFF RAM. No stack access occurs between
 * selecting cache bank 4 and restoring bank 1. Call with interrupts masked. */
void ew_store9(uint16_t offset,const void *packet) __sdcccall(0) NONBANKED NAKED {
 offset;packet;
 __asm
  push bc
  push de
  ldhl sp,#6
  ld a,(hl+)
  ld e,a
  ld a,(hl+)
  or a,#0xD0
  ld d,a
  ld a,(hl+)
  ld c,a
  ld h,(hl)
  ld l,c
  ld a,#4
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
void ew_load9(uint16_t offset,void *packet) __sdcccall(0) NONBANKED NAKED {
 offset;packet;
 __asm
  push bc
  push de
  ldhl sp,#6
  ld a,(hl+)
  ld c,a
  ld a,(hl+)
  or a,#0xD0
  ld b,a
  ld a,(hl+)
  ld e,a
  ld d,(hl)
  ld h,b
  ld l,c
  ld a,#4
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
