#ifndef EG_CLOCK_H
#define EG_CLOCK_H
/* EGRESS clock rules, shared by the cartridge and the host tests (host/test_clock.c). No hardware access here: the
 * calendar, the VBlank count, the kept-clock and HOST CLOCK records, and the MBC3 RTC arithmetic (counter seconds,
 * the 511-day carry fold, the tick probe). src/clock.c does the SRAM and the mapper.
 *
 * Time is kept in UTC: days since 2000-01-01, the minute of the day, seconds and an 18-bit fraction of a second. The
 * player's UTC offset (signed quarter hours) is for display and entry only (docs/redesign.md 9.7.1).
 *
 * Portable C for SDCC (SM83) and host compilers; 32-bit values only where seconds need them. */
#include <stdint.h>

#ifdef __SDCC
#include <gb/gb.h>
#define EGC_BANKED BANKED
#else
#define EGC_BANKED
#endif

#define EGC_YEAR0 2000u
#define EGC_YEAR_MAX 2135u   /* seconds since 2000 fit a u32 until early 2136 */
#define EGC_FRAME 4389u      /* one VBlank in 1/262144 s (70,224 clocks at 4,194,304 Hz) */
#define EGC_FRAC_BITS 18u
#define EGC_DAY 86400ul
#define EGC_RTC_SPAN (512ul * 86400ul) /* the MBC3 day counter's 9 bits */
#define EGC_OFF_MIN (-48)     /* UTC-12:00, in quarter hours */
#define EGC_OFF_MAX 56        /* UTC+14:00 */

typedef struct {
  uint16_t days;   /* UTC days since 2000-01-01 (a Saturday) */
  uint16_t minute; /* UTC minute of the day, < 1440 */
  uint8_t sec;     /* < 60 */
  uint32_t frac;   /* < 2^18, in 1/262144 s */
} egc_time;

typedef struct {
  uint16_t year;
  uint8_t month, day, hour, minute, sec; /* month 1..12, day 1..31 */
} egc_ymd;

/* --- calendar (2000 leaps; 2100 does not) --- */
uint8_t egc_leap(uint16_t year) EGC_BANKED;
uint8_t egc_mdays(uint16_t year, uint8_t month) EGC_BANKED;
uint16_t egc_days_from_ymd(uint16_t year, uint8_t month, uint8_t day) EGC_BANKED;
void egc_ymd_from_days(uint16_t days, egc_ymd *out) EGC_BANKED; /* fills year, month, day */

/* seconds since 2000-01-01 00:00 UTC (valid to early 2136) */
uint32_t egc_secs(const egc_time *t) EGC_BANKED;
void egc_from_secs(egc_time *t, uint32_t s) EGC_BANKED; /* frac is cleared */
void egc_add_secs(egc_time *t, uint32_t s) EGC_BANKED;
/* VBlanks counted while running: exact against the crystal, the remainder kept in t->frac */
void egc_add_frames(egc_time *t, uint16_t frames) EGC_BANKED;

/* local time (UTC + offset quarter hours) <-> UTC; to_utc returns 0 on an out-of-range date */
void egc_local(const egc_time *t, int8_t off_q, egc_ymd *out) EGC_BANKED;
uint8_t egc_from_local(egc_time *t, const egc_ymd *in, int8_t off_q) EGC_BANKED;

uint16_t egc_crc16(const uint8_t *p, uint8_t n) EGC_BANKED; /* CRC-16/CCITT-FALSE, as the meta record */

/* --- the kept clock: two alternating 16-byte records (9.7.1) --- */
#define EGC_KEPT_SIZE 16u
#define EGC_KEPT_VER 1u
#define EGC_KF_KNOWN 0x01u /* the clock was set (by the player, a host, or anchored to an RTC) */
#define EGC_KF_ASKED 0x02u /* the first-boot question was answered or skipped: never asked again by itself */
#define EGC_KF_HOST 0x04u  /* the last set came from a HOST CLOCK block */
typedef struct {
  egc_time t;
  int8_t off_q;   /* the player's UTC offset, quarter hours */
  uint8_t flags;  /* EGC_KF_* */
  uint8_t boots;  /* power-ons since the clock was last set (saturates at 15): the clock is stale after one */
  uint8_t seq;    /* the newer valid record wins (wrapping) */
} egc_kept;
void egc_kept_pack(const egc_kept *k, uint8_t *rec) EGC_BANKED;
uint8_t egc_kept_unpack(egc_kept *k, const uint8_t *rec) EGC_BANKED; /* 1 if magic, version, ranges and CRC hold */
/* the newer of two raw records: 0 none valid, 1 a, 2 b */
uint8_t egc_kept_pick(egc_kept *out, const uint8_t *a, const uint8_t *b) EGC_BANKED;

/* --- HOST CLOCK, 16 bytes, CRUCIBLE's layout, version 2 (9.7.1) ---
 * 0-3 "HCLK", 4 version 2, 5 flags (bit 0: byte 13 is the UTC offset), 6-7 year LE, 8 month, 9 day, 10 hour,
 * 11 minute, 12 second (the player's local time), 13 UTC offset in signed quarter hours, 14-15 CRC-16 of 0..13 LE */
#define EGC_HOST_SIZE 16u
uint8_t egc_host_parse(const uint8_t *blk, egc_time *t, int8_t *off_q, int8_t off_default) EGC_BANKED;

/* --- MBC3 RTC --- */
#define EGC_DH_DAY8 0x01u
#define EGC_DH_HALT 0x40u
#define EGC_DH_CARRY 0x80u
typedef struct {
  uint8_t s, m, h, dl, dh; /* registers 0x08..0x0C, as latched */
} egc_rtc_regs;
uint8_t egc_rtc_valid(const egc_rtc_regs *r) EGC_BANKED;  /* seconds/minutes < 60, hours < 24 */
uint32_t egc_rtc_secs(const egc_rtc_regs *r) EGC_BANKED;  /* the counter in seconds, 0 .. 512 days - 1 */
/* did the counter tick plausibly over `frames` VBlanks? (one read before the KEEL boot, one after it) */
uint8_t egc_rtc_ticks(uint32_t s0, uint32_t s1, uint16_t frames) EGC_BANKED;

/* the RTC block, 16 bytes: 0-1 "RT", 2 version 1, 3 flags, 4-7 base (UTC seconds since 2000 at counter 0, LE),
 * 8 folds done, 9 the player's offset when anchored, 10-13 zero, 14-15 CRC-16 of 0..13 LE */
#define EGC_RTC_SIZE 16u
#define EGC_RF_VALID 0x01u
#define EGC_RF_FOLDING 0x02u /* base already includes the next 512 days; the chip's carry may still be set */
#define EGC_RF_HOST 0x04u    /* anchored from a HOST CLOCK block */
typedef struct {
  uint32_t base;
  uint8_t flags, folds;
  int8_t off_q;
} egc_rtc_blk;
void egc_rtc_pack(const egc_rtc_blk *b, uint8_t *rec) EGC_BANKED;
uint8_t egc_rtc_unpack(egc_rtc_blk *b, const uint8_t *rec) EGC_BANKED;

/* The carry fold. The chip sets its carry bit when the day counter passes 511 and wraps to 0. Folding adds 512 days
 * to the base and clears the carry, in three steps so a power cut at any point neither loses nor doubles the 512 days:
 *   1. write the block with base + 512 days and FOLDING set,
 *   2. clear the chip's carry,
 *   3. write the block with FOLDING clear.
 * egc_rtc_fold_plan says which steps a boot still needs. */
#define EGC_FOLD_WRITE_PENDING 0x01u /* step 1 (the block is updated in place by the call) */
#define EGC_FOLD_CLEAR_CARRY 0x02u  /* step 2 */
#define EGC_FOLD_WRITE_DONE 0x04u   /* step 3 */
uint8_t egc_rtc_fold_plan(egc_rtc_blk *b, uint8_t dh) EGC_BANKED;

#endif
