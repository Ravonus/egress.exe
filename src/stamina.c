#pragma bank 255
#include "play.h"
#include "heir.h"
#include "family.h"
#include "stamina.h"
#include "tutorial.h"
eg_stamina __at(0xDBA0) stamina;
void stamina_begin(void) BANKED {eg_stamina_begin(&stamina,family_stat[G_VIGOR]);}
uint8_t stamina_move(uint8_t move,uint8_t evade,uint8_t level) BANKED {
  uint8_t cost=eg_stamina_cost(move,evade,level,heir.weapon);
  if(eg_stamina_spend(&stamina,cost)) return 1u;
  tutorial_queue(TE_STAMINA);sfx(SFX_DENY);puff(pl_x,(uint8_t)(pl_y+3u),2u,ST_PUFF);return 0u;
}
void stamina_tick(uint8_t busy) BANKED {eg_stamina_tick(&stamina,busy);}
