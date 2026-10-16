/* 8192 Hz, 4-bit wave-channel speech. The interrupt streams sixteen bytes at
 * 256 Hz; no per-sample interrupt, allocation, decompression or runtime network.
 * Wave-RAM sequencing follows GBDK-2020's wav_sample example (GBDK LGPL).
 * CGB double speed needs a 128-tick reload at TAC 07, not the DMG's 64. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "voice.h"
#include "gen/voice_assets.h"
volatile uint8_t voice_busy,voice_dirty;
uint8_t voice_clip,voice_seq,voice_on;
static uint8_t voice_bank;
static const uint8_t *voice_ptr;
static volatile uint16_t voice_blocks;
static void voice_isr(void) NONBANKED NAKED {
  __asm
    ld hl,#_voice_blocks
    ld a,(hl+)
    or (hl)
    jr nz,1$
    xor a
    ldh (_TAC_REG),a
    ldh (_NR30_REG),a
    ld (#_voice_busy),a
    inc a
    ld (#_voice_dirty),a
    ret
  1$:
    ld hl,#_voice_ptr
    ld a,(hl+)
    ld h,(hl)
    ld l,a
    ld a,(#__current_bank)
    ld e,a
    ld a,(#_voice_bank)
    ld (#0x2000),a
    ldh a,(_NR51_REG)
    ld c,a
    and #0xBB
    ldh (_NR51_REG),a
    xor a
    ldh (_NR30_REG),a
    .irp ofs,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
      ld a,(hl+)
      ldh (__AUD3WAVERAM+ofs),a
    .endm
    ld a,#0x80
    ldh (_NR30_REG),a
    ld a,#0xFE
    ldh (_NR31_REG),a
    ld a,#0x20
    ldh (_NR32_REG),a
    xor a
    ldh (_NR33_REG),a
    ld a,#0xC7
    ldh (_NR34_REG),a
    ld a,c
    ldh (_NR51_REG),a
    ld a,e
    ld (#0x2000),a
    ld a,l
    ld (#_voice_ptr),a
    ld a,h
    ld (#_voice_ptr+1),a
    ld hl,#_voice_blocks
    ld a,(hl)
    sub #1
    ld (hl+),a
    ld a,(hl)
    sbc #0
    ld (hl),a
    ret
  __endasm;
}
void voice_init(void) NONBANKED {
  CRITICAL {TAC_REG=0u;TMA_REG=TIMA_REG=0x80u;add_TIM(voice_isr);IE_REG|=TIM_IFLAG;}
  voice_busy=voice_dirty=voice_seq=0u;voice_clip=255u;
}
void voice_stop(void) NONBANKED {
  CRITICAL {TAC_REG=0u;voice_blocks=0u;if(voice_busy) {NR30_REG=0u;voice_busy=0u;voice_dirty=1u;}}
}
uint8_t voice_start(uint8_t clip) NONBANKED {
  if(!voice_on || voice_busy || clip>=8u) return 0u;
  CRITICAL {
    switch(clip) {
      case 0u:voice_bank=BANK(voice_data_0);voice_ptr=voice_data_0;voice_blocks=VOICE_BLOCKS_0;break;
      case 1u:voice_bank=BANK(voice_data_1);voice_ptr=voice_data_1;voice_blocks=VOICE_BLOCKS_1;break;
      case 2u:voice_bank=BANK(voice_data_2);voice_ptr=voice_data_2;voice_blocks=VOICE_BLOCKS_2;break;
      case 3u:voice_bank=BANK(voice_data_3);voice_ptr=voice_data_3;voice_blocks=VOICE_BLOCKS_3;break;
      case 4u:voice_bank=BANK(voice_data_4);voice_ptr=voice_data_4;voice_blocks=VOICE_BLOCKS_4;break;
      case 5u:voice_bank=BANK(voice_data_5);voice_ptr=voice_data_5;voice_blocks=VOICE_BLOCKS_5;break;
      case 6u:voice_bank=BANK(voice_data_6);voice_ptr=voice_data_6;voice_blocks=VOICE_BLOCKS_6;break;
      case 7u:voice_bank=BANK(voice_data_7);voice_ptr=voice_data_7;voice_blocks=VOICE_BLOCKS_7;break;
    }
    voice_clip=clip;voice_seq++;voice_dirty=0u;voice_busy=1u;
    TMA_REG=TIMA_REG=0x80u;TAC_REG=0x07u;
  }
  return 1u;
}
