/* Stage roles mapped to the canonical B2 mob runtime. */
#pragma bank 255
#include "stage_spawn.h"
#include "mob.h"

uint8_t ss_spawn(uint8_t fam, uint8_t cell, uint8_t elite, uint16_t seed) BANKED {
  return mob_spawn(fam, cell, elite, seed);
}
uint8_t ss_spawn_ok(uint8_t cell) BANKED { return mob_spawn_ok(cell); }
uint8_t ss_alive(void) BANKED { return mobs_alive(); }
void ss_preload(uint8_t fams) BANKED { mobs_preload(fams); }
