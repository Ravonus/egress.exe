/* b2-boss's arena entry, stubbed until that branch merges (src/stage_spawn.h). Delete this file and its Makefile
 * line when b2-boss provides boss_arena_enter. */
#pragma bank 255
#include <gb/gb.h>
#include "stage_spawn.h"

uint8_t boss_arena_enter(uint8_t slot, uint8_t level) BANKED {
  (void)slot;
  (void)level;
  return 0u; /* not handled: the stand-ins play */
}
