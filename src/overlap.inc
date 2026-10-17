/* Byte coordinates use absolute distance, never wrapped subtraction. Keep the
 * shared hitbox primitive in registers so pose work fits beside busy combat.
 * sdcccall(1): A=ax, E=ay; bx/by/rx/ry are four callee-popped stack bytes. */
uint8_t overlap(uint8_t ax,uint8_t ay,uint8_t bx,uint8_t by,uint8_t rx,uint8_t ry) NONBANKED NAKED {
  ax;ay;bx;by;rx;ry;
  __asm
    ld c,a
    ldhl sp,#2
    ld a,(hl)
    sub a,c
    jr nc,1$
    cpl
    inc a
  1$:
    ld b,a
    ldhl sp,#4
    ld a,(hl)
    cp a,b
    jr z,8$
    jr c,8$
    ldhl sp,#3
    ld a,(hl)
    sub a,e
    jr nc,2$
    cpl
    inc a
  2$:
    ld c,a
    ldhl sp,#5
    ld a,(hl)
    cp a,c
    jr z,8$
    jr c,8$
    ld a,#1
    jr 9$
  8$:
    xor a,a
  9$:
    pop hl
    add sp,#4
    jp (hl)
  __endasm;
}
