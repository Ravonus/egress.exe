/* EGM test ROM: CGB double speed, display on, nothing but the music driver. The PyBoy harness (render.py) drives it
 * through egm_ctl (one command per write of egm_ctl[0]).
 * CPU cost: interrupts are off; each frame starts on the VBlank flag, runs egm_tick(), then counts idle loop turns
 * until the next VBlank (egm_idle). With egm_ctl[6] = 1 the tick is skipped, which calibrates a whole idle frame;
 * the tick's share of the frame is 1 - idle / idle_calibrated. (PyBoy's DIV reads are not cycle-exact mid-frame,
 * so a DIV stopwatch gave impossible values; this method only needs the instruction timing and IF.) */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "egm.h"

volatile uint8_t egm_ctl[8];
volatile uint16_t egm_idle;

void main(void) {
  uint8_t seen = 0;
  uint16_t n;
  if (_cpu == CGB_TYPE) cpu_fast();
  DISPLAY_ON;
  disable_interrupts();
  egm_init();
  egm_ctl[0] = 0;
  egm_ctl[7] = 0xa5u; /* ready: the harness waits for this */
  while (1) {
    while (!(IF_REG & 1u)) {}
    IF_REG = 0;
    if (egm_ctl[0] != seen) {
      seen = egm_ctl[0];
      switch (egm_ctl[1]) {
        case 1: egm_play(egm_ctl[2], egm_ctl[3], (uint16_t)(egm_ctl[4] | ((uint16_t)egm_ctl[5] << 8))); break;
        case 2: egm_go(egm_ctl[2], egm_ctl[3], egm_ctl[4]); break;
        case 3: egm_intensity(egm_ctl[2]); break;
        case 4: egm_sfx(egm_ctl[2]); break;
        case 5: egm_music_volume(egm_ctl[2], egm_ctl[3]); break;
        case 6: egm_sfx_volume(egm_ctl[2]); break;
        case 7: egm_master(egm_ctl[2], egm_ctl[3]); break;
        case 8: egm_stop(); break;
      }
    }
    if (!egm_ctl[6]) egm_tick();
    n = 0;
    while (!(IF_REG & 1u)) n++;
    egm_idle = n;
  }
}
