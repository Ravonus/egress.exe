/* First hidden, unpresented cell in a bounded row/column. Callers provide
 * a nonempty interval entirely inside the 16x14 room. Shared verbatim with
 * the small native oracle cartridge; neither scan changes either bitmap. */
static uint8_t missing_column(uint8_t cell,uint8_t count) NAKED {
  cell;count;
  __asm
    ld c,a
    ld b,e
    push bc
    and a,#7
    ld l,a
    ld h,#0
    ld de,#_look_bits
    add hl,de
    ld c,(hl)
    ldhl sp,#0
    ld a,(hl)
    srl a
    srl a
    srl a
    ld e,a
    ld d,#0
    ld hl,#_look_present
    add hl,de
    push hl
    ld hl,#_roam_hidden
    add hl,de
    pop de
  1$:
    ld a,(de)
    cpl
    and a,(hl)
    and a,c
    jr nz,2$
    inc de
    inc de
    inc hl
    inc hl
    dec b
    jr nz,1$
    pop bc
    ld a,#255
    ret
  2$:
    ldhl sp,#1
    ld a,(hl-)
    sub a,b
    swap a
    add a,(hl)
    pop bc
    ret
  __endasm;
}

static uint8_t missing_row(uint8_t cell,uint8_t count) {
  uint8_t end=(uint8_t)(cell+count),at=cell>>3;
  uint8_t mask=(uint8_t)~(look_bits[cell&7u]-1u),bits;
  do {
    bits=(uint8_t)(roam_hidden[at] & (uint8_t)~look_present[at] & mask);
    if(bits) {
      cell&=248u;
      if(!(bits&15u)){bits>>=4;cell+=4u;}
      if(!(bits&3u)){bits>>=2;cell+=2u;}
      if(!(bits&1u))cell++;
      return cell<end ? cell : 255u;
    }
    cell=(uint8_t)((cell|7u)+1u);at++;mask=255u;
  } while(cell<end);
  return 255u;
}
