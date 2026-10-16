#ifndef EG_JOURNAL_H
#define EG_JOURNAL_H
#include "eg_room.h"
/* Version at 0, serial at 1, CRC-16/CCITT-FALSE in the last two bytes. */
uint16_t eg_save_crc(const uint8_t *p, uint16_t n) EG_BANKED;
uint8_t eg_save_valid(const uint8_t *p, uint16_t n, uint8_t version) EG_BANKED;
uint8_t eg_save_newer(uint8_t a, uint8_t b) EG_BANKED;
void eg_save_seal(uint8_t *p, uint16_t n) EG_BANKED;
#endif
