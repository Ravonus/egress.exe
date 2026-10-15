/* Sky engine cost probe (CGB double speed, MBC5). Times each call with the timer (TAC 262144 Hz, 524288 Hz in
 * double speed: one tick = 16 CPU clocks at 8.388608 MHz) plus an overflow count, and leaves the results and the
 * computed positions in WRAM for measure.py. The screen shows "DONE" when finished. */
#include <gb/gb.h>
#include <gb/cgb.h>

#include "sky.h"

#define NT 8
volatile uint16_t t_hi;
uint32_t res_ticks[NT]; /* ticks per call: 0 positions, 1 read in range, 2 read outside, 3 find (phase, late), 4 find (pluto
                           ingress), 5 natal chart + aspects, 6 transit scan, 7 mark lookup */
uint16_t res_pos[SKY_NPOS];
uint16_t res_read[6];
uint8_t done;
static uint16_t natal[SKY_NPOS], now_[SKY_NPOS];
static sky_now sn;

static void tim_isr(void) { t_hi++; }
static uint32_t ticks(void) {
  uint16_t h;
  uint8_t l;
  do { h = t_hi; l = TIMA_REG; } while (h != t_hi);
  return ((uint32_t)h << 8) | l;
}

void main(void) {
  uint32_t t0;
  uint8_t i, j, k, hits = 0;
  uint16_t miss;
  sky_ev a, b;
  if (_cpu == CGB_TYPE) cpu_fast();
  disable_interrupts();
  TMA_REG = 0; TIMA_REG = 0; TAC_REG = 0x05; /* 262144 Hz */
  add_TIM(tim_isr);
  set_interrupts(TIM_IFLAG); /* only the timer while measuring: its overflow ISR costs ~1% */
  enable_interrupts();
  /* 2026-10-15 00:48 local: the reading the doc uses (days since 2000: 9784) */
  t0 = ticks(); sky_positions(9784u, 48u, res_pos); res_ticks[0] = ticks() - t0;
  t0 = ticks(); sky_read(9784u, 48u, &sn); res_ticks[1] = ticks() - t0;
  res_read[0] = sn.flags; res_read[1] = sn.phase8; res_read[2] = sn.retro; res_read[3] = sn.moon_sign;
  res_read[4] = (uint16_t)sn.voc_end; res_read[5] = sn.eclipse;
  t0 = ticks(); sky_read(47000u, 600u, &sn); res_ticks[2] = ticks() - t0; /* 2128: the fallback */
  t0 = ticks(); sky_find(SKY_S_PHASE, 53000000ul, &a, &b); res_ticks[3] = ticks() - t0;
  t0 = ticks(); sky_find(SKY_S_IN_MERCURY, 53000000ul, &a, &b); res_ticks[4] = ticks() - t0;
  /* a natal chart (positions + the 15 aspects among Sun, Moon, Mercury, Venus, Mars, Ascendant) */
  t0 = ticks();
  sky_positions(9000u, 1000u, natal);
  for (i = 0; i < 7u; i++) for (j = (uint8_t)(i + 1u); j < 9u; j++) {
    if (i == SKY_NODE || j == SKY_NODE) continue;
    if (sky_aspect(natal[i], natal[j], SKY_DEG(6), &miss) != 255u) hits++;
  }
  res_ticks[5] = ticks() - t0;
  /* a transit scan: now's Sun..Saturn against the natal Sun..Mars + Ascendant, per-planet orbs */
  t0 = ticks();
  sky_positions(9784u, 48u, now_);
  for (i = 0; i < SKY_NPLANET; i++) for (k = 0; k < 7u; k++) {
    j = k == 6u ? SKY_ASC : k;
    if (sky_aspect(now_[i], natal[j], i == SKY_MOON ? SKY_DEG(4) : SKY_DEG(2), &miss) != 255u) hits++;
  }
  res_ticks[6] = ticks() - t0;
  t0 = ticks(); (void)sky_mark(4000u, 0); res_ticks[7] = ticks() - t0;
  done = (uint8_t)(1u + (hits & 1u));
  set_interrupts(VBL_IFLAG);

  while (1) wait_vbl_done();
}
