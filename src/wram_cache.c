/* CGB scene cache. Only the naked leaves ever run with SVBK != 1. They do not
 * touch the stack or call anything while the stack's bank is hidden. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "wram_cache.h"
static void cache_read4(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED NAKED {
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
    ld b,a
  1$:
    ld a,#4
    ldh (#0x70),a
    ld a,(hl+)
    ld c,a
    ld a,#1
    ldh (#0x70),a
    ld a,c
    ld (de),a
    inc de
    dec b
    jr nz,1$
    pop de
    pop bc
    ret
  __endasm;
}
static void cache_read5(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED NAKED {
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
    ld b,a
  1$:
    ld a,#5
    ldh (#0x70),a
    ld a,(hl+)
    ld c,a
    ld a,#1
    ldh (#0x70),a
    ld a,c
    ld (de),a
    inc de
    dec b
    jr nz,1$
    pop de
    pop bc
    ret
  __endasm;
}
static void cache_write4(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED NAKED {
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
    ld b,a
  1$:
    ld a,(hl+)
    ld c,a
    ld a,#4
    ldh (#0x70),a
    ld a,c
    ld (de),a
    inc de
    ld a,#1
    ldh (#0x70),a
    dec b
    jr nz,1$
    pop de
    pop bc
    ret
  __endasm;
}
static void cache_write5(void *dst,const void *src,uint8_t n) __sdcccall(0) NONBANKED NAKED {
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
    ld b,a
  1$:
    ld a,(hl+)
    ld c,a
    ld a,#5
    ldh (#0x70),a
    ld a,c
    ld (de),a
    inc de
    ld a,#1
    ldh (#0x70),a
    dec b
    jr nz,1$
    pop de
    pop bc
    ret
  __endasm;
}
uint8_t wram_put(uint8_t sector,uint16_t off,const void *src,uint16_t n) NONBANKED {
  uint8_t *p;const uint8_t *s=src;uint16_t chunk;
  if(sector>=WRAM_SECTORS || n>WRAM_SECTOR_BYTES || off>WRAM_SECTOR_BYTES-n) return 0u;
  p=(uint8_t *)(0xD000u+((uint16_t)(sector&3u)<<10)+off);
  while(n) {
    chunk=n>256u ? 256u : n;
    CRITICAL {if(sector<4u) cache_write4(p,s,(uint8_t)chunk);else cache_write5(p,s,(uint8_t)chunk);}
    p+=chunk;s+=chunk;n-=chunk;
  }
  return 1u;
}
uint8_t wram_get(void *dst,uint8_t sector,uint16_t off,uint16_t n) NONBANKED {
  uint8_t *d=dst;const uint8_t *p;uint16_t chunk;
  if(sector>=WRAM_SECTORS || n>WRAM_SECTOR_BYTES || off>WRAM_SECTOR_BYTES-n) return 0u;
  p=(const uint8_t *)(0xD000u+((uint16_t)(sector&3u)<<10)+off);
  while(n) {
    chunk=n>256u ? 256u : n;
    CRITICAL {if(sector<4u) cache_read4(d,p,(uint8_t)chunk);else cache_read5(d,p,(uint8_t)chunk);}
    d+=chunk;p+=chunk;n-=chunk;
  }
  return 1u;
}
