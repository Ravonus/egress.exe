/* The cartridge's clock: SRAM records, the MBC3 RTC (RTC build), the clock and SETUP screens. See clock.h. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "egress.h"
#include "mapper.h"
#include "clock.h"
#include "play.h"
#include "gen/art.h"

#define SRAM_KEPT_A 0xB4C0u
#define SRAM_KEPT_B 0xB4D0u
#define SRAM_HOST 0xB4E0u
#define SRAM_RTC 0xB4F0u

egc_time clk_now;
int8_t clk_off;
uint8_t clk_quality, clk_known, clk_drift, clk_rtc, dbg_rtc;

static egc_kept kept;
static uint16_t last_vbl, unsaved; /* unsaved: VBlanks since the last kept-clock record */
static uint32_t boot_secs; /* the kept clock at power-on: an RTC reading earlier than this has lost time */

static uint16_t vbl_now(void) {
  uint16_t v;
  CRITICAL { v = sys_time; }
  return v;
}

static void sram_read(uint16_t at, uint8_t *dst) {
  EG_SRAM_OPEN();
  memcpy(dst, (const uint8_t *)at, 16);
  EG_SRAM_CLOSE();
}
static void sram_write(uint16_t at, const uint8_t *src) {
  EG_SRAM_OPEN();
  memcpy((uint8_t *)at, src, 16);
  EG_SRAM_CLOSE();
}

/* every 5 minutes of play the kept clock is written, so a power-off loses at most that much of it */
#define SAVE_EVERY 17920u

void clock_poll(void) BANKED {
  uint16_t now = vbl_now(), n = (uint16_t)(now - last_vbl);
  last_vbl = now;
  if (!n) return;
  egc_add_frames(&clk_now, n);
  unsaved = (uint16_t)(unsaved + n);
  if (unsaved >= SAVE_EVERY && clk_known) clock_save();
}

/* the next generation goes to the slot the newest is not in */
void clock_save(void) BANKED {
  uint8_t rec[16];
  unsaved = 0;
  clock_poll();
  unsaved = 0;
  kept.t = clk_now;
  kept.off_q = clk_off;
  kept.seq++;
  egc_kept_pack(&kept, rec);
  sram_write((kept.seq & 1u) ? SRAM_KEPT_B : SRAM_KEPT_A, rec);
}

#if EG_HAS_RTC
static egc_rtc_blk rblk;
static uint8_t rblk_ok, rtc_s0_ok;
static uint32_t rtc_s0;
static uint16_t probe_vbl;

static void rtc_wait(void) {
  volatile uint8_t i;
  for (i = 0; i != 4u; i++) {
  }
}

/* latch, then read registers 0x08..0x0C through the RTC port */
static void rtc_read(egc_rtc_regs *r) {
  uint8_t *p = (uint8_t *)r, reg;
  EG_SRAM_ON();
  EG_RTC_LATCH();
  rtc_wait();
  for (reg = 0x08u; reg != 0x0Du; reg++) {
    EG_RTC_SELECT(reg);
    rtc_wait();
    *p++ = EG_RTC_PORT;
  }
  EG_SRAM_BANK(0);
  EG_SRAM_OFF();
  if (dbg_rtc) memset(r, 0xff, sizeof *r);
}

/* day high, halt and carry: only ever written to a chip whose registers read valid */
static void rtc_write_dh(uint8_t v) {
  EG_SRAM_ON();
  EG_RTC_SELECT(0x0Cu);
  rtc_wait();
  EG_RTC_PORT = v;
  rtc_wait();
  EG_SRAM_BANK(0);
  EG_SRAM_OFF();
}

static void rblk_write(void) {
  uint8_t rec[16];
  egc_rtc_pack(&rblk, rec);
  sram_write(SRAM_RTC, rec);
}

/* the clock was just set: anchor the chip to it (start a halted chip, drop a stale carry) */
static void rtc_anchor(uint8_t host) {
  egc_rtc_regs r;
  rtc_read(&r);
  if (!egc_rtc_valid(&r)) return;
  if (r.dh & (EGC_DH_HALT | EGC_DH_CARRY)) {
    rtc_write_dh((uint8_t)(r.dh & EGC_DH_DAY8));
    rtc_read(&r);
    if (!egc_rtc_valid(&r)) return;
  }
  clock_poll();
  rblk.base = egc_secs(&clk_now) - egc_rtc_secs(&r);
  rblk.flags = (uint8_t)(EGC_RF_VALID | (host ? EGC_RF_HOST : 0u));
  rblk.folds = 0;
  rblk.off_q = clk_off;
  rblk_ok = 1;
  rblk_write();
}
#endif

static void clock_set(const egc_time *t, int8_t off, uint8_t quality) {
  clk_now = *t;
  clk_off = off;
  clk_quality = quality;
  clk_known = 1;
  clk_drift = 0;
  last_vbl = vbl_now();
  kept.flags = (uint8_t)(EGC_KF_KNOWN | EGC_KF_ASKED | (quality == CLK_HOST ? EGC_KF_HOST : 0u));
  kept.boots = 0;
#if EG_HAS_RTC
  rtc_anchor(quality == CLK_HOST);
#endif
  clock_save();
}

void clock_init(void) BANKED {
  uint8_t a[16], b[16];
  egc_time t;
  int8_t off;
  sram_read(SRAM_KEPT_A, a);
  sram_read(SRAM_KEPT_B, b);
  if (egc_kept_pick(&kept, a, b)) {
    if (kept.boots < 15u) kept.boots++;
  } else {
    memset(&kept, 0, sizeof kept);
    kept.t.days = 9497u; /* unset: 2026-01-01 08:00 UTC */
    kept.t.minute = 480u;
  }
  clk_now = kept.t;
  clk_off = kept.off_q;
  clk_known = (uint8_t)(kept.flags & EGC_KF_KNOWN);
  clk_quality = CLK_STALE;
  boot_secs = egc_secs(&kept.t);
  last_vbl = vbl_now();
#if EG_HAS_RTC
  {
    egc_rtc_regs r;
    sram_read(SRAM_RTC, a);
    rblk_ok = egc_rtc_unpack(&rblk, a);
    rtc_read(&r);
    rtc_s0_ok = egc_rtc_valid(&r);
    rtc_s0 = egc_rtc_secs(&r);
    probe_vbl = vbl_now();
  }
#endif
  /* a HOST CLOCK block: set silently, then consumed (the magic zeroed) */
  sram_read(SRAM_HOST, a);
  if (egc_host_parse(a, &t, &off, clk_off)) {
    a[0] = 0;
    sram_write(SRAM_HOST, a);
    clock_set(&t, off, CLK_HOST);
  } else if (kept.seq || clk_known) {
    clock_save(); /* the power-on count */
  }
}

void clock_probe(void) BANKED {
#if EG_HAS_RTC
  egc_rtc_regs r;
  uint32_t s1, now;
  uint8_t plan;
  clk_rtc = RTC_UNANCHORED;
  if (rblk_ok) {
    clk_rtc = RTC_DEAD;
    rtc_read(&r);
    s1 = egc_rtc_secs(&r);
    if (rtc_s0_ok && egc_rtc_valid(&r) && !(r.dh & EGC_DH_HALT) &&
        egc_rtc_ticks(rtc_s0, s1, (uint16_t)(vbl_now() - probe_vbl))) {
      plan = egc_rtc_fold_plan(&rblk, r.dh);
      if (plan & EGC_FOLD_WRITE_PENDING) rblk_write();
      if (plan & EGC_FOLD_CLEAR_CARRY) rtc_write_dh((uint8_t)(r.dh & EGC_DH_DAY8));
      if (plan & EGC_FOLD_WRITE_DONE) {
        rblk.flags &= (uint8_t)~EGC_RF_FOLDING;
        rblk_write();
      }
      now = rblk.base + s1;
      /* a chip that reads earlier than the kept clock lost power: not trusted */
      if (!clk_known || now + 120u >= boot_secs) {
        clk_rtc = RTC_OK;
        if (clk_quality == CLK_STALE) { /* a HOST CLOCK this boot already set it */
          egc_from_secs(&clk_now, now);
          last_vbl = vbl_now();
          clk_quality = (rblk.flags & EGC_RF_HOST) ? CLK_HOST : CLK_PLAYER;
          kept.boots = 0;
          clock_save();
        }
      }
    }
  }
#endif
  clk_drift = (uint8_t)(clk_known && clk_quality == CLK_STALE);
}

uint8_t clock_needs_ask(void) BANKED { return (uint8_t)!(kept.flags & EGC_KF_ASKED); }

/* --- screens --- */
static void put2(char *p, uint8_t v) {
  p[0] = (char)('0' + v / 10u);
  p[1] = (char)('0' + v % 10u);
}

/* "2026-10-15  08:07" */
static void fmt_date(char *s, const egc_ymd *v) {
  uint16_t y = v->year;
  s[0] = (char)('0' + y / 1000u);
  s[1] = (char)('0' + (y / 100u) % 10u);
  put2(s + 2, (uint8_t)(y % 100u));
  s[4] = '-';
  put2(s + 5, v->month);
  s[7] = '-';
  put2(s + 8, v->day);
  s[10] = 0;
}
static void fmt_time(char *s, const egc_ymd *v) {
  put2(s, v->hour);
  s[2] = ':';
  put2(s + 3, v->minute);
  s[5] = 0;
}
/* "+05:30" */
static void fmt_off(char *s, int8_t q) {
  uint8_t a = (uint8_t)(q < 0 ? -q : q);
  s[0] = q < 0 ? '-' : '+';
  put2(s + 1, (uint8_t)(a / 4u));
  s[3] = ':';
  put2(s + 4, (uint8_t)((a & 3u) * 15u));
  s[6] = 0;
}

/* the title's clock row as text (18 characters): the 3D title draws it in its own font */
void clock_title_text(uint8_t phase, char *s) BANKED {
  egc_ymd v;
  memset(s, ' ', 18);
  s[18] = 0;
  if (!clk_known) {
    memcpy(s, " SELECT: SET CLOCK", 18);
  } else if (clk_drift && (phase & 3u) == 1u) {
    memcpy(s + 1, "MAY HAVE DRIFTED", 16);
  } else if (clk_drift && (phase & 3u) == 2u) {
    memcpy(s + 1, "SELECT TO SET IT", 16);
  } else {
    clock_poll();
    egc_local(&clk_now, clk_off, &v);
    fmt_date(s + 1, &v);
    s[11] = ' ';
    fmt_time(s + 13, &v);
  }
}

void clock_title_line(uint8_t phase) BANKED {
  char s[19];
  clock_title_text(phase, s);
  text_at(0, 1, 16, s, 5);
}

static void screen_open(void) {
  uint8_t i;
  DISPLAY_OFF;
  HIDE_SPRITES;
  HIDE_WIN;
  hud_line = 144u;
  move_bkg(0, 0);
  for (i = 0; i != 40u; i++) move_sprite(i, 0, 0);
  art_load_game();
  art_copy_tables(&meta_tile[0][0], meta_pal, pal_bg, pal_sp, people);
  clear_map(0, 4);
  pal_bg[5 * 4] = pal_bg[4 * 4];
  pal_bg[7 * 4] = pal_bg[4 * 4];
}

static void screen_show(void) {
  pal_commit(0);
  SHOW_BKG;
  DISPLAY_ON;
  fade_to(0, 8);
}

/* fields: year, month, day, hour, minute, zone */
static const uint8_t field_x[6] = {8, 13, 16, 8, 11, 11};
static const uint8_t field_y[6] = {5, 5, 5, 8, 8, 11};
static const uint8_t field_w[6] = {4, 2, 2, 2, 2, 6};

static void draw_values(const egc_ymd *v, int8_t off, uint8_t field) {
  char s[11];
  uint8_t k;
  fmt_date(s, v);
  text_at(0, 8, 5, s, 4);
  fmt_time(s, v);
  text_at(0, 8, 8, s, 4);
  fmt_off(s, off);
  text_at(0, 11, 11, s, 4);
  for (k = 0; k != 3u; k++) text_at(0, 8, (uint8_t)(6u + k * 3u), "          ", 5);
  memset(s, '-', field_w[field]);
  s[field_w[field]] = 0;
  text_at(0, field_x[field], (uint8_t)(field_y[field] + 1u), s, 5);
}

static void step(egc_ymd *v, int8_t *off, uint8_t field, int8_t d) {
  uint8_t md;
  switch (field) {
  case 0:
    v->year = (uint16_t)(v->year + d);
    if (v->year < EGC_YEAR0) v->year = EGC_YEAR_MAX;
    if (v->year > EGC_YEAR_MAX) v->year = EGC_YEAR0;
    break;
  case 1:
    v->month = (uint8_t)(v->month + d);
    if (!v->month) v->month = 12u;
    if (v->month > 12u) v->month = 1u;
    break;
  case 2:
    md = egc_mdays(v->year, v->month);
    v->day = (uint8_t)(v->day + d);
    if (!v->day) v->day = md;
    if (v->day > md) v->day = 1u;
    break;
  case 3:
    v->hour = (uint8_t)(v->hour + d);
    if (v->hour == 255u) v->hour = 23u;
    if (v->hour > 23u) v->hour = 0;
    break;
  case 4:
    v->minute = (uint8_t)(v->minute + d);
    if (v->minute == 255u) v->minute = 59u;
    if (v->minute > 59u) v->minute = 0;
    break;
  default:
    *off = (int8_t)(*off + d);
    if (*off < EGC_OFF_MIN) *off = EGC_OFF_MAX;
    if (*off > EGC_OFF_MAX) *off = EGC_OFF_MIN;
  }
  md = egc_mdays(v->year, v->month);
  if (v->day > md) v->day = md;
}

void clock_screen(uint8_t first) BANKED {
  egc_ymd v;
  egc_time t;
  int8_t off = clk_off;
  uint8_t field = 0, held = 0, f0;
  game_mode = MODE_SETUP;
  clock_poll();
  egc_local(&clk_now, off, &v);
  v.sec = 0;
  screen_open();
  text_at(0, 2, 2, first ? "WHAT IS TODAY?" : "SET THE CLOCK", 7);
  text_at(0, 2, 5, "DATE", 5);
  text_at(0, 2, 8, "TIME", 5);
  text_at(0, 2, 11, "ZONE", 5);
  text_at(0, 8, 11, "UTC", 4);
  text_at(0, 1, 14, "PAD: FIELD, VALUE", 5);
  text_at(0, 1, 15, first ? "A KEEP  B SKIP" : "A KEEP  B BACK", 5);
  draw_values(&v, off, field);
  screen_show();
  while (1) {
    frame_wait();
    sound_tick();
    input_poll();
    f0 = field;
    if (joy_pressed & J_LEFT) field = field ? (uint8_t)(field - 1u) : 5u;
    if (joy_pressed & J_RIGHT) field = field == 5u ? 0u : (uint8_t)(field + 1u);
    /* UP/DOWN: once, then repeating after a third of a second */
    if (joy & (J_UP | J_DOWN)) {
      if ((joy_pressed & (J_UP | J_DOWN)) || (held > 20u && !(held & 3u))) {
        step(&v, &off, field, (joy & J_UP) ? 1 : -1);
        f0 = 255u;
      }
      if (held != 255u) held++;
    } else {
      held = 0;
    }
    if (joy_pressed & (J_LEFT | J_RIGHT | J_UP | J_DOWN)) sfx(SFX_STEP);
    if (f0 != field) draw_values(&v, off, field);
    if ((joy_pressed & (J_A | J_START)) && egc_from_local(&t, &v, off)) {
      sfx(SFX_KEY);
      clock_set(&t, off, CLK_PLAYER);
      break;
    }
    if (joy_pressed & J_B) {
      if (first) {
        kept.flags |= EGC_KF_ASKED;
        clock_save();
      }
      break;
    }
  }
  fade_to(8, 0);
}

void setup_screen(void) BANKED {
  uint8_t sel = 0, k, n;
  char s[19];
  egc_ymd v;
  while (1) {
    game_mode = MODE_SETUP;
    screen_open();
    text_at(0, 2, 2, "SETUP", 7);
    text_at(0, 3, 6, "CLOCK", 4);
    text_at(0, 3, 8, "BACK", 4);
    text_at(0, 1, 15, "A PICK  B BACK", 5);
    n = 0;
    while (1) {
      for (k = 0; k != 2u; k++) text_at(0, 1, (uint8_t)(6u + k * 2u), k == sel ? ">" : " ", 5);
      if (!(n & 31u)) { /* the clock row, twice a second */
        clock_poll();
        egc_local(&clk_now, clk_off, &v);
        memset(s, ' ', 18);
        s[18] = 0;
        if (clk_known) {
          fmt_date(s, &v);
          s[10] = ' ';
          fmt_time(s + 11, &v);
          s[16] = ' ';
          if (clk_quality == CLK_STALE) s[17] = '?';
        } else {
          memcpy(s, "NOT SET", 7);
        }
        text_at(0, 3, 11, s, 5);
        if (!n) screen_show();
      }
      n = (uint8_t)((n + 1u) | 0x80u); /* never 0 again */
      frame_wait();
      sound_tick();
      input_poll();
      if ((joy_pressed & J_UP) && sel) sel--;
      if ((joy_pressed & J_DOWN) && !sel) sel++;
      if (joy_pressed & (J_UP | J_DOWN)) sfx(SFX_STEP);
      if (joy_pressed & J_B) sel = 1u;
      if (joy_pressed & (J_A | J_B | J_START)) break;
    }
    fade_to(8, 0);
    if (sel == 1u) return;
    clock_screen(0);
  }
}
