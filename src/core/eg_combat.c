#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_combat.h"
/* Applied state takes precedence over the body's intrinsic element. Tools and hits share this rule. */
uint8_t eg_combat_reaction(uint8_t tags, uint8_t element, uint8_t status, uint8_t shock) EG_BANKED {
  uint8_t k,rx;
  if ((status & MS_WET) && shock) return RX_SHORT;
  if ((tags & TG(TAG_COLD)) && (status & MS_WET)) return RX_FREEZE;
  if ((tags & TG(TAG_COLD)) && (status & MS_COLD)) return RX_DEEPFREEZE;
  if ((tags & TG(TAG_HOT)) && (status & MS_FROZEN)) return RX_THAW;
  if ((tags & TG(TAG_HOT)) && (status & MS_WET)) return RX_STEAM;
  for(k=0u;k!=EG_TAGS;k++) if(tags & TG(k)) {
    rx=eg_rx(k,element);
    if(rx) return rx;
  }
  return RX_NONE;
}
