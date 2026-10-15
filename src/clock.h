#ifndef CLOCK_H
#define CLOCK_H
/* The cartridge's clock (docs/redesign.md 9.7.1 and 16b). The rules are in src/core/eg_clock.c; this does the SRAM,
 * the RTC chip and the screens.
 *
 *   both builds  a set-once clock: asked once (date, time, UTC offset), counted with VBlanks while running, kept in
 *                SRAM, re-set from SETUP; a HOST CLOCK block written by a host before power-on sets it silently
 *   RTC build    the same, plus an MBC3 RTC anchored at the set: read every boot, so it survives power-off. A chip
 *                that does not tick, reads invalid values, or has gone back in time falls back to the set-once clock.
 *
 * SRAM (bank 0): kept clock A 0xB4C0, B 0xB4D0; HOST CLOCK 0xB4E0; RTC block 0xB4F0 (RTC build only); 0xBFFF is the
 * RTC access byte. */
#include <gb/gb.h>
#include <stdint.h>
#include "core/eg_clock.h"

enum { CLK_STALE = 0, CLK_PLAYER = 1, CLK_HOST = 2 }; /* the stamp quality of 9.7.3 (3, seeded, is not the cart's) */
enum { RTC_NONE = 0, RTC_UNANCHORED = 1, RTC_OK = 2, RTC_DEAD = 3 };

extern egc_time clk_now;     /* UTC, advanced by clock_poll */
extern int8_t clk_off;       /* the player's UTC offset, quarter hours */
extern uint8_t clk_quality;  /* CLK_*: STALE once resumed from SRAM after a power-off */
extern uint8_t clk_known;    /* the clock was ever set */
extern uint8_t clk_drift;    /* the title shows the gentle "may have drifted" prompt */
extern uint8_t clk_rtc;      /* RTC_*: what the boot probe found (RTC_NONE on the MBC5 build) */
extern uint8_t dbg_rtc;      /* harness hook: 1 makes every RTC read return 0xFF (open bus) */

void clock_init(void) BANKED;  /* power-on, before the KEEL boot: the kept clock, a HOST CLOCK, the first RTC read */
void clock_probe(void) BANKED; /* after the KEEL boot: the second RTC read decides RTC or fallback */
void clock_poll(void) BANKED;  /* count the VBlanks since the last poll (any gap up to 18 minutes) */
void clock_save(void) BANKED;  /* the next kept-clock record */
uint8_t clock_needs_ask(void) BANKED;
void clock_screen(uint8_t first) BANKED; /* WHAT IS TODAY? (first boot) / SET THE CLOCK (SETUP) */
void setup_screen(void) BANKED;
void clock_title_line(uint8_t phase) BANKED; /* the title's clock row */
void clock_title_text(uint8_t phase, char *s) BANKED; /* the same row as 18 characters into s[19] */

#endif
