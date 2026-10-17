/* Office combat can reuse the inactive world cache (WRAM 2..5). Generated
 * worlds own those banks exclusively. Two ways retain 120 complete poses;
 * metadata occupies the unused end of bank five, outside all payload slots.
 * Every bank-switched leaf is stack-free and restores SVBK before returning. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "heir.h"
extern uint8_t eg_window_packet[9];
extern uint8_t heir_cache_enabled;
static uint8_t way;

static void clear_keys(void) NAKED {
  __asm
    ldh a,(_SVBK_REG)
    ld c,a
    ld a,#5
    ldh (_SVBK_REG),a
    ld hl,#0xde00
    ld b,#120
    ld a,#255
  1$:
    ld (hl+),a
    dec b
    jr nz,1$
    ld a,c
    ldh (_SVBK_REG),a
    ret
  __endasm;
}

/* packet: pose, direction, slot, mask; 4..8 remain available to their owner. */
static uint8_t read_pose(void) NAKED {
  __asm
    ldh a,(_SVBK_REG)
    ld (#_eg_window_packet+4),a
    ld a,#5
    ldh (_SVBK_REG),a
    ld a,(#_eg_window_packet+2)
    ld l,a
    ld h,#0xde
    ld a,(#_eg_window_packet)
    cp a,(hl)
    jr nz,8$
    set 7,l
    ld a,(#_eg_window_packet+1)
    cp a,(hl)
    jr nz,8$
    ld h,#0xdf
    res 7,l
    ld a,(hl)
    ld (#_eg_window_packet+3),a
    ld a,(#_eg_window_packet+2)
    ld c,a
    srl a
    srl a
    srl a
    srl a
    srl a
    add a,#2
    ldh (_SVBK_REG),a
    ld a,c
    and a,#31
    srl a
    add a,#0xd0
    ld h,a
    ld l,#0
    bit 0,c
    jr z,1$
    ld l,#128
  1$:
    ld de,#_heir_pixels
    ld b,#16
  2$:
    .rept 8
      ld a,(hl+)
      ld (de),a
      inc de
    .endm
    dec b
    jr nz,2$
    ld a,(#_eg_window_packet+4)
    ldh (_SVBK_REG),a
    ld a,(#_eg_window_packet+3)
    or a,#128
    ret
  8$:
    ld a,(#_eg_window_packet+4)
    ldh (_SVBK_REG),a
    xor a,a
    ret
  __endasm;
}
static void write_pose(void) NAKED {
  __asm
    ldh a,(_SVBK_REG)
    ld (#_eg_window_packet+4),a
    ld a,#5
    ldh (_SVBK_REG),a
    ld a,(#_eg_window_packet+2)
    ld l,a
    ld h,#0xde
    ld a,(#_eg_window_packet)
    ld (hl),a
    set 7,l
    ld a,(#_eg_window_packet+1)
    ld (hl),a
    ld h,#0xdf
    res 7,l
    ld a,(#_eg_window_packet+3)
    ld (hl),a
    ld a,(#_eg_window_packet+2)
    ld c,a
    srl a
    srl a
    srl a
    srl a
    srl a
    add a,#2
    ldh (_SVBK_REG),a
    ld a,c
    and a,#31
    srl a
    add a,#0xd0
    ld d,a
    ld e,#0
    bit 0,c
    jr z,1$
    ld e,#128
  1$:
    ld hl,#_heir_pixels
    ld b,#16
  2$:
    .rept 8
      ld a,(hl+)
      ld (de),a
      inc e
    .endm
    dec b
    jr nz,2$
    ld a,(#_eg_window_packet+4)
    ldh (_SVBK_REG),a
    ret
  __endasm;
}
static uint8_t slot_for(uint8_t pose,uint8_t dd) {
  uint8_t slot=(uint8_t)(pose+dd*23u)&63u;
  return slot>=60u ? (uint8_t)(slot-60u) : slot;
}
void heir_cache_reset(uint8_t on) BANKED {
  heir_cache_enabled=on;way=0u;
  if(on) {CRITICAL {clear_keys();}}
}
uint8_t heir_cache_get(uint8_t pose,uint8_t dd) BANKED {
  uint8_t hit;
  if(!heir_cache_enabled)return 0u;
  eg_window_packet[0]=pose;eg_window_packet[1]=dd;eg_window_packet[2]=slot_for(pose,dd);
  CRITICAL {
    hit=read_pose();
    if(!hit) {eg_window_packet[2]+=60u;hit=read_pose();}
  }
  return hit;
}
void heir_cache_put(uint8_t pose,uint8_t dd,uint8_t mask) BANKED {
  if(!heir_cache_enabled)return;
  way^=1u;
  eg_window_packet[0]=pose;eg_window_packet[1]=dd;
  eg_window_packet[2]=(uint8_t)(slot_for(pose,dd)+(way ? 60u : 0u));eg_window_packet[3]=mask;
  CRITICAL {write_pose();}
}
