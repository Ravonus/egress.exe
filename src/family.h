#ifndef FAMILY_H
#define FAMILY_H
#include "core/eg_family.h"
extern eg_family __at(0xDA40) family;
extern uint8_t family_cur, family_stat[G_STATS];
void family_prepare(void) BANKED;
void family_begin(void) BANKED;
void family_end(uint8_t won) BANKED;
void family_apply(void) BANKED;
#endif
