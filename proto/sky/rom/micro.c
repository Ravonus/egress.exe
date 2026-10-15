/* micro-benchmarks for the sky engine's primitives (100 calls each) */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "sky.h"
volatile uint16_t t_hi;
uint16_t res_ticks[8];
uint8_t done;
volatile int32_t sink32;
volatile int16_t sink16;
volatile uint16_t va = 12345u, vb = 23456u;
volatile int16_t vs = -1234, vt = 9999;
static void tim_isr(void) { t_hi++; }
static uint32_t ticks(void) { uint16_t h; uint8_t l; do { h = t_hi; l = TIMA_REG; } while (h != t_hi); return ((uint32_t)h << 8) | l; }
void main(void) {
  uint32_t t0; uint8_t i;
  if (_cpu == CGB_TYPE) cpu_fast();
  disable_interrupts(); TMA_REG = 0; TIMA_REG = 0; TAC_REG = 0x05; add_TIM(tim_isr); set_interrupts(TIM_IFLAG); enable_interrupts();
  t0 = ticks(); for (i = 0; i < 100u; i++) sink32 = sky_mul16(vs, vt); res_ticks[0] = (uint16_t)(ticks() - t0);
  t0 = ticks(); for (i = 0; i < 100u; i++) sink32 = sky_mulu16(va, vb); res_ticks[1] = (uint16_t)(ticks() - t0);
  t0 = ticks(); for (i = 0; i < 100u; i++) sink16 = sky_sin(va); res_ticks[2] = (uint16_t)(ticks() - t0);
  t0 = ticks(); for (i = 0; i < 100u; i++) sink16 = (int16_t)sky_atan2(vs, vt); res_ticks[3] = (uint16_t)(ticks() - t0);
  t0 = ticks(); for (i = 0; i < 100u; i++) sink16 = (int16_t)(vs * (int8_t)3); res_ticks[4] = (uint16_t)(ticks() - t0);
  t0 = ticks(); for (i = 0; i < 100u; i++) sink32 = sink32 >> 14; res_ticks[5] = (uint16_t)(ticks() - t0);
  t0 = ticks(); for (i = 0; i < 100u; i++) sink32 = sink32 + vt; res_ticks[6] = (uint16_t)(ticks() - t0);
  t0 = ticks(); for (i = 0; i < 100u; i++) ; res_ticks[7] = (uint16_t)(ticks() - t0);
  done = 1;
  while (1) ;
}
