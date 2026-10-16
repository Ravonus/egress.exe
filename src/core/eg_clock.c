/* EGRESS clock rules (see eg_clock.h): no hardware access, shared with the host tests. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_clock.h"

static const uint8_t MDAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

uint8_t egc_leap(uint16_t year) EGC_BANKED {
  return (uint8_t)(!(year & 3u) && ((year % 100u) || !(year % 400u)));
}

uint8_t egc_mdays(uint16_t year, uint8_t month) EGC_BANKED {
  if (month < 1u || month > 12u) return 0;
  return (uint8_t)(MDAYS[month - 1u] + (month == 2u && egc_leap(year)));
}

uint16_t egc_days_from_ymd(uint16_t year, uint8_t month, uint8_t day) EGC_BANKED {
  uint16_t n = 0, y;
  uint8_t m;
  for (y = EGC_YEAR0; y < year; y++) n = (uint16_t)(n + 365u + egc_leap(y));
  for (m = 1; m < month; m++) n = (uint16_t)(n + egc_mdays(year, m));
  return (uint16_t)(n + day - 1u);
}

void egc_ymd_from_days(uint16_t days, egc_ymd *out) EGC_BANKED {
  uint16_t y = EGC_YEAR0, len;
  uint8_t m = 1, ml;
  while (days >= (len = (uint16_t)(365u + egc_leap(y)))) {
    days = (uint16_t)(days - len);
    y++;
  }
  while (days >= (ml = egc_mdays(y, m))) {
    days = (uint16_t)(days - ml);
    m++;
  }
  out->year = y;
  out->month = m;
  out->day = (uint8_t)(days + 1u);
}

uint32_t egc_secs(const egc_time *t) EGC_BANKED {
  return (uint32_t)t->days * EGC_DAY + (uint32_t)t->minute * 60ul + t->sec;
}

void egc_from_secs(egc_time *t, uint32_t s) EGC_BANKED {
  uint32_t r;
  t->days = (uint16_t)(s / EGC_DAY);
  r = s - (uint32_t)t->days * EGC_DAY;
  t->minute = (uint16_t)(r / 60u);
  t->sec = (uint8_t)(r - (uint32_t)t->minute * 60u);
  t->frac = 0;
}

void egc_add_secs(egc_time *t, uint32_t s) EGC_BANKED {
  uint32_t f = t->frac;
  egc_from_secs(t, egc_secs(t) + s);
  t->frac = f;
}

void egc_add_frames(egc_time *t, uint16_t frames) EGC_BANKED {
  uint32_t f = t->frac + (uint32_t)frames * EGC_FRAME;
  uint16_t s = (uint16_t)(f >> EGC_FRAC_BITS);
  t->frac = f & ((1ul << EGC_FRAC_BITS) - 1u);
  if (!s) return;
  /* A uint16 frame gap is at most 1,098 seconds. Carry at most nineteen
   * minutes directly; converting the whole date through 32-bit division at a
   * minute boundary could consume half a gameplay frame on the SM83. */
  s=(uint16_t)(s+t->sec);
  while(s>=60u) {
    s-=60u;
    if(++t->minute==1440u) {t->minute=0u;t->days++;}
  }
  t->sec=(uint8_t)s;
}

void egc_local(const egc_time *t, int8_t off_q, egc_ymd *out) EGC_BANKED {
  int32_t m = (int32_t)t->days * 1440 + (int32_t)t->minute + (int32_t)off_q * 15;
  uint16_t d, mod;
  if (m < 0) m = 0;
  d = (uint16_t)(m / 1440);
  mod = (uint16_t)(m - (int32_t)d * 1440);
  egc_ymd_from_days(d, out);
  out->hour = (uint8_t)(mod / 60u);
  out->minute = (uint8_t)(mod % 60u);
  out->sec = t->sec;
}

uint8_t egc_from_local(egc_time *t, const egc_ymd *in, int8_t off_q) EGC_BANKED {
  int32_t m;
  if (in->year < EGC_YEAR0 || in->year > EGC_YEAR_MAX || in->month < 1u || in->month > 12u || in->day < 1u ||
      in->day > egc_mdays(in->year, in->month) || in->hour > 23u || in->minute > 59u || in->sec > 59u ||
      off_q < EGC_OFF_MIN || off_q > EGC_OFF_MAX)
    return 0;
  m = (int32_t)egc_days_from_ymd(in->year, in->month, in->day) * 1440 + (int32_t)in->hour * 60 + in->minute -
      (int32_t)off_q * 15;
  if (m < 0) m = 0;
  t->days = (uint16_t)(m / 1440);
  t->minute = (uint16_t)(m - (int32_t)t->days * 1440);
  t->sec = in->sec;
  t->frac = 0;
  return 1;
}

uint16_t egc_crc16(const uint8_t *p, uint8_t n) EGC_BANKED {
  uint16_t c = 0xffffu;
  uint8_t k;
  while (n--) {
    c ^= (uint16_t)(*p++) << 8;
    for (k = 0; k != 8u; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
  }
  return c;
}

static void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static void seal(uint8_t *rec) { put16(rec + 14, egc_crc16(rec, 14u)); }
static uint8_t sealed(const uint8_t *rec) { return (uint8_t)(get16(rec + 14) == egc_crc16(rec, 14u)); }

/* kept clock: 0-1 "CK", 2 version, 3 flags (bits 0-2 EGC_KF_*, bits 4-7 boots), 4 seq, 5 offset, 6-7 days,
 * 8-9 minute, 10 seconds, 11-13 fraction (18 bits), 14-15 CRC */
void egc_kept_pack(const egc_kept *k, uint8_t *rec) EGC_BANKED {
  rec[0] = 'C';
  rec[1] = 'K';
  rec[2] = EGC_KEPT_VER;
  rec[3] = (uint8_t)((k->flags & 0x0fu) | ((k->boots > 15u ? 15u : k->boots) << 4));
  rec[4] = k->seq;
  rec[5] = (uint8_t)k->off_q;
  put16(rec + 6, k->t.days);
  put16(rec + 8, k->t.minute);
  rec[10] = k->t.sec;
  rec[11] = (uint8_t)k->t.frac;
  rec[12] = (uint8_t)(k->t.frac >> 8);
  rec[13] = (uint8_t)((k->t.frac >> 16) & 3u);
  seal(rec);
}

uint8_t egc_kept_unpack(egc_kept *k, const uint8_t *rec) EGC_BANKED {
  int8_t off = (int8_t)rec[5];
  if (rec[0] != 'C' || rec[1] != 'K' || rec[2] != EGC_KEPT_VER || !sealed(rec)) return 0;
  if (get16(rec + 8) >= 1440u || rec[10] >= 60u || rec[13] > 3u || off < EGC_OFF_MIN || off > EGC_OFF_MAX) return 0;
  k->flags = (uint8_t)(rec[3] & 0x0fu);
  k->boots = (uint8_t)(rec[3] >> 4);
  k->seq = rec[4];
  k->off_q = off;
  k->t.days = get16(rec + 6);
  k->t.minute = get16(rec + 8);
  k->t.sec = rec[10];
  k->t.frac = (uint32_t)rec[11] | ((uint32_t)rec[12] << 8) | ((uint32_t)rec[13] << 16);
  return 1;
}

uint8_t egc_kept_pick(egc_kept *out, const uint8_t *a, const uint8_t *b) EGC_BANKED {
  egc_kept ka, kb;
  uint8_t oka = egc_kept_unpack(&ka, a), okb = egc_kept_unpack(&kb, b);
  if (oka && (!okb || (int8_t)(ka.seq - kb.seq) >= 0)) {
    *out = ka;
    return 1;
  }
  if (okb) {
    *out = kb;
    return 2;
  }
  return 0;
}

uint8_t egc_host_parse(const uint8_t *blk, egc_time *t, int8_t *off_q, int8_t off_default) EGC_BANKED {
  egc_ymd in;
  int8_t off = off_default;
  if (blk[0] != 'H' || blk[1] != 'C' || blk[2] != 'L' || blk[3] != 'K' || blk[4] != 2u || !sealed(blk)) return 0;
  if (blk[5] & 1u) off = (int8_t)blk[13];
  in.year = get16(blk + 6);
  in.month = blk[8];
  in.day = blk[9];
  in.hour = blk[10];
  in.minute = blk[11];
  in.sec = blk[12];
  if (!egc_from_local(t, &in, off)) return 0;
  *off_q = off;
  return 1;
}

uint8_t egc_rtc_valid(const egc_rtc_regs *r) EGC_BANKED {
  return (uint8_t)(r->s < 60u && r->m < 60u && r->h < 24u);
}

uint32_t egc_rtc_secs(const egc_rtc_regs *r) EGC_BANKED {
  uint16_t d = (uint16_t)(r->dl | ((uint16_t)(r->dh & EGC_DH_DAY8) << 8));
  return (uint32_t)d * EGC_DAY + (uint32_t)r->h * 3600u + (uint16_t)r->m * 60u + r->s;
}

/* Expected seconds from the VBlank count; the chip must have moved at least one second and not wildly more (an LCD-off
 * stretch or a slow emulator only makes the VBlank count low, so the upper bound is loose). The counter wraps after
 * 512 days; a wrap between the two reads is accepted. */
uint8_t egc_rtc_ticks(uint32_t s0, uint32_t s1, uint16_t frames) EGC_BANKED {
  uint32_t e = ((uint32_t)frames * EGC_FRAME) >> EGC_FRAC_BITS, d;
  d = s1 >= s0 ? s1 - s0 : s1 + EGC_RTC_SPAN - s0;
  return (uint8_t)(d >= 1u && d <= e * 4u + 4u);
}

void egc_rtc_pack(const egc_rtc_blk *b, uint8_t *rec) EGC_BANKED {
  uint8_t i;
  rec[0] = 'R';
  rec[1] = 'T';
  rec[2] = 1u;
  rec[3] = b->flags;
  rec[4] = (uint8_t)b->base;
  rec[5] = (uint8_t)(b->base >> 8);
  rec[6] = (uint8_t)(b->base >> 16);
  rec[7] = (uint8_t)(b->base >> 24);
  rec[8] = b->folds;
  rec[9] = (uint8_t)b->off_q;
  for (i = 10; i != 14u; i++) rec[i] = 0;
  seal(rec);
}

uint8_t egc_rtc_unpack(egc_rtc_blk *b, const uint8_t *rec) EGC_BANKED {
  if (rec[0] != 'R' || rec[1] != 'T' || rec[2] != 1u || !sealed(rec) || !(rec[3] & EGC_RF_VALID)) return 0;
  b->flags = rec[3];
  b->base = (uint32_t)rec[4] | ((uint32_t)rec[5] << 8) | ((uint32_t)rec[6] << 16) | ((uint32_t)rec[7] << 24);
  b->folds = rec[8];
  b->off_q = (int8_t)rec[9];
  return 1;
}

uint8_t egc_rtc_fold_plan(egc_rtc_blk *b, uint8_t dh) EGC_BANKED {
  if (b->flags & EGC_RF_FOLDING) /* a fold was cut short: the base already has the 512 days */
    return (uint8_t)(((dh & EGC_DH_CARRY) ? EGC_FOLD_CLEAR_CARRY : 0u) | EGC_FOLD_WRITE_DONE);
  if (!(dh & EGC_DH_CARRY)) return 0;
  b->base += EGC_RTC_SPAN;
  b->folds++;
  b->flags |= EGC_RF_FOLDING;
  return EGC_FOLD_WRITE_PENDING | EGC_FOLD_CLEAR_CARRY | EGC_FOLD_WRITE_DONE;
}
