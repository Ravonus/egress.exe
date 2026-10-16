/* Host tests for the clock rules (src/core/eg_clock.c): `test_clock`.
 *
 *  - the calendar round-trips every day from 2000 to 2135; leap years (2000 yes, 2100 no); known dates
 *  - the VBlank count is exact: N frames add exactly N * 4389 / 262144 s, however they are chunked
 *  - local time <-> UTC across day and year boundaries; out-of-range entries are refused
 *  - kept-clock and RTC records round-trip; a flip of any byte is refused; the newer sequence wins, across the wrap
 *  - HOST CLOCK version 2 blocks: the offset flag, a bad CRC, an impossible date
 *  - the RTC counter, the tick probe, and the 511-day carry fold, including a power cut after every fold step */
#include <stdio.h>
#include <string.h>

#include "../src/core/eg_clock.h"

static int failures, checks;
#define CHECK(c, ...)                                        \
  do {                                                       \
    checks++;                                                \
    if (!(c)) {                                              \
      failures++;                                            \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
      fprintf(stderr, __VA_ARGS__);                          \
      fputc('\n', stderr);                                   \
    }                                                        \
  } while (0)

static void test_calendar(void) {
  egc_ymd d;
  uint16_t n, bad = 0;
  CHECK(egc_leap(2000) && !egc_leap(2100) && egc_leap(2024) && !egc_leap(2026), "leap years");
  CHECK(egc_days_from_ymd(2000, 1, 1) == 0, "epoch");
  CHECK(egc_days_from_ymd(2026, 1, 1) == 9497, "2026-01-01 is day 9497");
  CHECK(egc_days_from_ymd(2026, 10, 15) == 9784, "2026-10-15 is day 9784");
  CHECK(egc_days_from_ymd(2100, 3, 1) - egc_days_from_ymd(2100, 2, 28) == 1, "2100 has no Feb 29");
  for (n = 0; n <= egc_days_from_ymd(2135, 12, 31); n++) {
    egc_ymd_from_days(n, &d);
    if (egc_days_from_ymd(d.year, d.month, d.day) != n || d.day < 1 || d.day > egc_mdays(d.year, d.month)) bad++;
  }
  CHECK(bad == 0, "the calendar round-trips every day 2000..2135 (%u bad)", bad);
}

static void test_frames(void) {
  static const uint16_t chunk[] = {1, 7, 255, 3584, 65535};
  unsigned i;
  for (i = 0; i < sizeof chunk / sizeof chunk[0]; i++) {
    egc_time t = {9784, 1439, 59, 0};
    uint64_t start = (uint64_t)egc_secs(&t) << 18, total = 0, have;
    uint32_t n = 0;
    while (n < 200000u) {
      uint16_t k = chunk[i];
      egc_add_frames(&t, k);
      n += k;
      total += (uint64_t)k * EGC_FRAME;
    }
    have = ((uint64_t)egc_secs(&t) << 18) + t.frac;
    CHECK(have == start + total, "frames in chunks of %u are exact", chunk[i]);
  }
  {
    uint32_t gap,bad=0u;
    /* Every possible poll gap, varying fractional carry and crossing midnight.
     * This independently checks the bounded minute-carry fast path. */
    for(gap=0u;gap<=65535u;gap++) {
      egc_time t={9784u,1439u,59u,(gap*17u)&262143u};
      uint64_t want=((uint64_t)egc_secs(&t)<<18)+t.frac+(uint64_t)gap*EGC_FRAME;
      egc_add_frames(&t,(uint16_t)gap);
      if((((uint64_t)egc_secs(&t)<<18)+t.frac)!=want) bad++;
    }
    CHECK(!bad,"all 65536 frame gaps preserve midnight and fractional carry (%u bad)",(unsigned)bad);
  }
}

static void test_local(void) {
  egc_ymd in = {2026, 10, 15, 8, 7, 30}, out;
  egc_time t;
  CHECK(egc_from_local(&t, &in, -20), "UTC-5 entry");
  CHECK(t.days == 9784 && t.minute == 13 * 60 + 7 && t.sec == 30, "08:07 at UTC-5 is 13:07 UTC");
  egc_local(&t, -20, &out);
  CHECK(out.year == 2026 && out.month == 10 && out.day == 15 && out.hour == 8 && out.minute == 7, "and back");
  in = (egc_ymd){2026, 12, 31, 23, 30, 0};
  CHECK(egc_from_local(&t, &in, -8) && t.days == egc_days_from_ymd(2027, 1, 1) && t.minute == 90,
        "23:30 at UTC-2 on Dec 31 is 01:30 UTC next year");
  in = (egc_ymd){2027, 1, 1, 0, 15, 0};
  CHECK(egc_from_local(&t, &in, 22) && t.days == egc_days_from_ymd(2026, 12, 31) && t.minute == 1440 - 330 + 15,
        "00:15 at UTC+5:30 is the day before in UTC");
  in = (egc_ymd){2026, 2, 29, 0, 0, 0};
  CHECK(!egc_from_local(&t, &in, 0), "Feb 29 2026 is refused");
  in = (egc_ymd){2136, 1, 1, 0, 0, 0};
  CHECK(!egc_from_local(&t, &in, 0), "2136 is refused");
  in = (egc_ymd){2026, 1, 1, 0, 0, 0};
  CHECK(!egc_from_local(&t, &in, 57), "an offset past UTC+14 is refused");
}

static void test_kept(void) {
  egc_kept k = {{9784, 787, 41, 0x2abcd}, -20, EGC_KF_KNOWN | EGC_KF_ASKED, 3, 250}, o;
  uint8_t a[16], b[16], i, bad = 0;
  egc_kept_pack(&k, a);
  CHECK(egc_kept_unpack(&o, a) && !memcmp(&o.t, &k.t, sizeof o.t) && o.off_q == -20 && o.flags == k.flags &&
            o.boots == 3 && o.seq == 250,
        "kept record round-trips");
  for (i = 0; i < 16; i++) {
    uint8_t c[16];
    memcpy(c, a, 16);
    c[i] ^= 0x10;
    if (egc_kept_unpack(&o, c)) bad++;
  }
  CHECK(bad == 0, "a flipped byte is refused (%u accepted)", bad);
  k.seq = 3; /* 250 -> 3 wraps: 3 is newer */
  k.t.sec = 42;
  egc_kept_pack(&k, b);
  CHECK(egc_kept_pick(&o, a, b) == 2 && o.t.sec == 42, "the newer sequence wins across the wrap");
  memset(b, 0xff, 16);
  CHECK(egc_kept_pick(&o, a, b) == 1, "a blank record loses");
  memset(a, 0, 16);
  CHECK(egc_kept_pick(&o, a, b) == 0, "zeroed and 0xFF SRAM hold no clock");
}

static void host_block(uint8_t *h, uint16_t y, uint8_t mo, uint8_t d, uint8_t hh, uint8_t mi, uint8_t s, int off) {
  uint16_t c;
  memcpy(h, "HCLK", 4);
  h[4] = 2;
  h[5] = off == 99 ? 0 : 1;
  h[6] = (uint8_t)y;
  h[7] = (uint8_t)(y >> 8);
  h[8] = mo;
  h[9] = d;
  h[10] = hh;
  h[11] = mi;
  h[12] = s;
  h[13] = off == 99 ? 0 : (uint8_t)(int8_t)off;
  c = egc_crc16(h, 14);
  h[14] = (uint8_t)c;
  h[15] = (uint8_t)(c >> 8);
}

static void test_host(void) {
  uint8_t h[16];
  egc_time t;
  int8_t off = 0;
  host_block(h, 2026, 10, 15, 11, 10, 30, -20);
  CHECK(egc_host_parse(h, &t, &off, 4) && off == -20 && t.days == 9784 && t.minute == 16 * 60 + 10 && t.sec == 30,
        "a version 2 block with an offset");
  host_block(h, 2026, 10, 15, 11, 10, 30, 99);
  CHECK(egc_host_parse(h, &t, &off, 4) && off == 4 && t.minute == 11 * 60 + 10 - 60, "no offset flag: the default");
  host_block(h, 2026, 10, 15, 11, 10, 30, 0);
  h[15] ^= 1;
  CHECK(!egc_host_parse(h, &t, &off, 0), "a bad CRC is ignored");
  host_block(h, 2026, 2, 30, 11, 10, 30, 0);
  CHECK(!egc_host_parse(h, &t, &off, 0), "Feb 30 is ignored");
  host_block(h, 2026, 10, 15, 11, 10, 30, 0);
  h[4] = 1;
  CHECK(!egc_host_parse(h, &t, &off, 0), "version 1 is ignored");
}

/* a model MBC3 chip: a seconds counter that wraps at 512 days and sets a sticky carry */
typedef struct {
  uint64_t counter; /* seconds since its last zero, unwrapped */
  uint8_t carry;
} chip;
static void chip_run(chip *c, uint64_t s) {
  c->counter += s;
  while (c->counter >= EGC_RTC_SPAN) {
    c->counter -= EGC_RTC_SPAN;
    c->carry = 1;
  }
}
static egc_rtc_regs chip_latch(const chip *c) {
  egc_rtc_regs r;
  uint32_t s = (uint32_t)c->counter, d = s / EGC_DAY;
  s -= d * EGC_DAY;
  r.h = (uint8_t)(s / 3600);
  r.m = (uint8_t)(s / 60 % 60);
  r.s = (uint8_t)(s % 60);
  r.dl = (uint8_t)d;
  r.dh = (uint8_t)((d >> 8) & 1) | (c->carry ? EGC_DH_CARRY : 0);
  return r;
}

/* one boot: fold (stopping after `cut` steps: 0 none, 1 after step 1, 2 after step 2), return the time it reads */
static uint32_t boot(chip *c, uint8_t *sram_blk, int cut) {
  egc_rtc_blk b;
  egc_rtc_regs r = chip_latch(c);
  uint8_t plan;
  if (!egc_rtc_unpack(&b, sram_blk)) return 0;
  plan = egc_rtc_fold_plan(&b, r.dh);
  if (plan & EGC_FOLD_WRITE_PENDING) egc_rtc_pack(&b, sram_blk);
  if (cut == 1 && (plan & EGC_FOLD_WRITE_PENDING)) return 0;
  if (plan & EGC_FOLD_CLEAR_CARRY) c->carry = 0;
  if (cut == 2 && (plan & EGC_FOLD_CLEAR_CARRY)) return 0;
  if (plan & EGC_FOLD_WRITE_DONE) {
    b.flags &= (uint8_t)~EGC_RF_FOLDING;
    egc_rtc_pack(&b, sram_blk);
  }
  return b.base + egc_rtc_secs(&r);
}

static void test_rtc(void) {
  egc_rtc_regs r = {59, 59, 23, 0xff, 0x01};
  egc_rtc_blk b = {0x12345678u, EGC_RF_VALID | EGC_RF_HOST, 2, -20}, o;
  uint8_t rec[16];
  int cut;
  CHECK(egc_rtc_valid(&r) && egc_rtc_secs(&r) == EGC_RTC_SPAN - 1, "the counter's last second");
  r.s = 60;
  CHECK(!egc_rtc_valid(&r), "60 seconds is invalid");
  r = (egc_rtc_regs){0xff, 0xff, 0xff, 0xff, 0xff};
  CHECK(!egc_rtc_valid(&r), "open bus is invalid");
  CHECK(egc_rtc_ticks(100, 102, 150), "2 s over 150 VBlanks ticks");
  CHECK(!egc_rtc_ticks(100, 100, 150), "a stopped counter does not");
  CHECK(!egc_rtc_ticks(100, 4000, 150), "a jump of an hour does not");
  CHECK(egc_rtc_ticks(EGC_RTC_SPAN - 1, 1, 150), "a wrap between the reads ticks");
  egc_rtc_pack(&b, rec);
  CHECK(egc_rtc_unpack(&o, rec) && o.base == b.base && o.flags == b.flags && o.folds == 2 && o.off_q == -20,
        "RTC block round-trips");
  rec[5] ^= 4;
  CHECK(!egc_rtc_unpack(&o, rec), "a flipped base byte is refused");

  for (cut = 0; cut <= 2; cut++) {
    chip c = {12345, 0};
    uint8_t blk[16];
    uint32_t real = 9784u * EGC_DAY + 12u * 3600u, got; /* 2026-10-15 12:00 UTC */
    egc_rtc_regs at = chip_latch(&c);
    egc_rtc_blk a = {real - egc_rtc_secs(&at), EGC_RF_VALID, 0, 0};
    egc_rtc_pack(&a, blk);
    chip_run(&c, 600u * EGC_DAY + 77u); /* off for 600 days: the counter wraps once */
    real += 600u * EGC_DAY + 77u;
    got = boot(&c, blk, cut);
    if (cut) got = boot(&c, blk, 0); /* the power came back */
    CHECK(got == real, "600 days off, cut after step %d: %u, want %u", cut, (unsigned)got, (unsigned)real);
    CHECK(!c.carry, "the carry is clear afterwards (cut %d)", cut);
    chip_run(&c, 500u * EGC_DAY);
    real += 500u * EGC_DAY;
    got = boot(&c, blk, 0);
    CHECK(got == real, "a second wrap folds again (cut %d)", cut);
    egc_rtc_unpack(&o, blk);
    CHECK(o.folds == 2 && !(o.flags & EGC_RF_FOLDING), "two folds recorded (cut %d)", cut);
  }
}

int main(void) {
  test_calendar();
  test_frames();
  test_local();
  test_kept();
  test_host();
  test_rtc();
  printf("test_clock: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
