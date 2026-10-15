#include <gb/gb.h>
#include <gb/cgb.h>
#include "sky.h"
uint16_t sky_prof_mean(uint8_t i, uint16_t d, uint16_t m);
int32_t sky_prof_series(uint8_t t0, uint8_t n);
uint16_t sky_prof_lon(uint8_t b);
int16_t sky_prof_rad(uint8_t b);
volatile uint16_t t_hi;
uint32_t res_ticks[8];
uint8_t done;
volatile int32_t sink32;
volatile uint16_t sink16;
static void tim_isr(void) { t_hi++; }
static uint32_t ticks(void) { uint16_t h; uint8_t l; do { h = t_hi; l = TIMA_REG; } while (h != t_hi); return ((uint32_t)h << 8) | l; }
void main(void) {
  uint32_t t0; uint8_t i; uint16_t p[SKY_NPOS];
  if (_cpu == CGB_TYPE) cpu_fast();
  disable_interrupts(); TMA_REG = 0; TIMA_REG = 0; TAC_REG = 0x05; add_TIM(tim_isr); set_interrupts(TIM_IFLAG); enable_interrupts();
  sky_positions(9784u, 48u, p);
  t0 = ticks(); for (i = 0; i < 32u; i++) sink16 = sky_prof_mean(i, 9784u, 48u); res_ticks[0] = ticks() - t0;
  t0 = ticks(); sink32 = sky_prof_series(0, 20); res_ticks[1] = ticks() - t0;
  t0 = ticks(); for (i = 0; i < 7u; i++) sink16 = sky_prof_lon(i); res_ticks[2] = ticks() - t0;
  t0 = ticks(); for (i = 0; i < 7u; i++) if (i != 1) sink16 = (uint16_t)sky_prof_rad(i); res_ticks[3] = ticks() - t0;
  t0 = ticks(); for (i = 0; i < 100u; i++) sink16 = (uint16_t)sky_sinf(i * 600u); res_ticks[4] = ticks() - t0;
  t0 = ticks(); for (i = 0; i < 100u; i++) sink16 = (uint16_t)sky_sin(i * 601u); res_ticks[5] = ticks() - t0;
  t0 = ticks(); sky_positions(9784u, 48u, p); res_ticks[6] = ticks() - t0;
  done = 1;
  while (1) ;
}
