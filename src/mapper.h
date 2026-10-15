#ifndef MAPPER_H
#define MAPPER_H
/* The one place the game touches the cartridge mapper. Both builds come from the same source:
 *
 *   make            build/egress.gbc      MBC5 + RAM + battery (0x1B), the set-once VBlank clock
 *   make rtc        build/egress-rtc.gbc  MBC3 + timer + RAM + battery (0x10), the RTC with the VBlank fallback
 *
 * The Makefile passes -DEG_MBC3 for the RTC build. Registers (Pan Docs):
 *
 *   0x0000-0x1FFF  RAM (and RTC) enable: 0x0A on, 0x00 off                         both
 *   0x2000-0x2FFF  ROM bank, low 8 bits (MBC5); 0x2000-0x3FFF ROM bank, 7 bits (MBC3)
 *   0x3000-0x3FFF  ROM bank bit 8 (MBC5 only). On MBC3 this range is still the ROM bank register, so the MBC5
 *                  high-bit write (SWITCH_ROM_MBC5's rROMB1 = 0) would select bank 1. Never written by this game.
 *   0x4000-0x5FFF  RAM bank 0..15 (MBC5); RAM bank 0..3 or RTC register 0x08..0x0C (MBC3)
 *   0x6000-0x7FFF  nothing (MBC5); RTC latch 0 then 1 (MBC3)
 *
 * The game only ever uses ROM banks 1..114 (one write to 0x2000; GBDK's SWITCH_ROM, its banked-call trampolines and
 * far pointers do exactly that) and SRAM bank 0. tools/mapper_audit.py checks every build for any other write. */
#include <gb/gb.h>
#include <gb/hardware.h>

#ifdef EG_MBC3
#define EG_HAS_RTC 1
#else
#define EG_HAS_RTC 0
#endif

#define EG_SRAM_ON() (rRAMG = 0x0Au)
#define EG_SRAM_OFF() (rRAMG = 0x00u)
#define EG_SRAM_BANK(b) (rRAMB = (uint8_t)(b)) /* 0 only; on MBC3 0x08..0x0C select the RTC (src/clock.c) */
#define EG_ROM_BANK(b) SWITCH_ROM(b)           /* 0x2000 only, both mappers */

/* SRAM bank 0, enabled, for a save access; and off again */
#define EG_SRAM_OPEN() \
  do {                 \
    EG_SRAM_ON();      \
    EG_SRAM_BANK(0);   \
  } while (0)
#define EG_SRAM_CLOSE() EG_SRAM_OFF()

#if EG_HAS_RTC
/* MBC3 RTC access goes through 0xBFFF, the last SRAM byte (kept free for it): on a flash cart that maps the RTC
 * selects onto SRAM instead, a stray read or write lands there and not in a save record. */
#define EG_RTC_PORT (*(volatile uint8_t *)0xBFFFu)
#define EG_RTC_SELECT(reg) (rRAMB = (uint8_t)(reg)) /* 0x08 s, 0x09 m, 0x0A h, 0x0B day low, 0x0C day high/halt/carry */
#define EG_RTC_LATCH()                         \
  do {                                         \
    *(volatile uint8_t *)0x6000u = 0x00u;      \
    *(volatile uint8_t *)0x6000u = 0x01u;      \
  } while (0)
/* the MBC5 high ROM bank write must never reach an MBC3 build */
#undef SWITCH_ROM_MBC5
#define SWITCH_ROM_MBC5(b) error_mbc5_rom_high_bit_write_on_mbc3
#undef SWITCH_ROM_MBC5_8M
#define SWITCH_ROM_MBC5_8M(b) error_mbc5_rom_high_bit_write_on_mbc3
#endif

#endif
