#ifndef EG_COMBAT_H
#define EG_COMBAT_H
#include "eg_piece.h"
#define MS_WET 0x01u
#define MS_SHOCK 0x02u
#define MS_COLD 0x04u
#define MS_FROZEN 0x08u
#define MS_BURN 0x10u
#define MS_STUN 0x20u
#define MS_UNDER 0x40u
#define MS_BLIND 0x80u
uint8_t eg_combat_reaction(uint8_t tags, uint8_t element, uint8_t status, uint8_t shock) EG_BANKED;
#endif
