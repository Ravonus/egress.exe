/* The one piece of the sky engine that must sit in bank 0: a copy out of another ROM bank (the event streams live in
 * their own banks; the engine runs from its own switchable bank and calls this). */
#include <string.h>
#include "sky.h"
#ifdef __SDCC
#include <gb/gb.h>
void sky_copy(uint8_t bank, const uint8_t *src, uint8_t n, uint8_t *dst) {
  uint8_t ob = CURRENT_BANK;
  SWITCH_ROM(bank);
  while (n--) *dst++ = *src++;
  SWITCH_ROM(ob);
}
#else
void sky_copy(uint8_t bank, const uint8_t *src, uint8_t n, uint8_t *dst) { (void)bank; memcpy(dst, src, n); }
#endif
