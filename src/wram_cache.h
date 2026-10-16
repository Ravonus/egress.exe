#ifndef WRAM_CACHE_H
#define WRAM_CACHE_H
#include <gb/gb.h>
/* Eight 1 KiB scene sectors in CGB WRAM banks 4 and 5. Bank 1 and its live stack
 * remain selected between small interrupt-masked transfers. No bank switch escapes a leaf. */
#define WRAM_SECTORS 8u
#define WRAM_SECTOR_BYTES 1024u
uint8_t wram_put(uint8_t sector,uint16_t off,const void *src,uint16_t n) NONBANKED;
uint8_t wram_get(void *dst,uint8_t sector,uint16_t off,uint16_t n) NONBANKED;
#endif
