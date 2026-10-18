#ifndef EG_ELEMENTS_H
#define EG_ELEMENTS_H
#include <gb/gb.h>
#include "core/eg_catalogue.h"
extern uint8_t __at(0xDDEE) elemental_active;
extern uint8_t __at(0xDDEF) elemental_ward;
uint8_t elements_protect(uint8_t damage) BANKED;
#define ST_BOMB 120u
#define ELEMENT_ORB_BASE 128u
void elements_use(void) BANKED;
void elements_tick(void) BANKED;
void elements_draw(void) BANKED;
uint8_t elements_target(uint8_t effect,uint8_t cell,uint8_t power,uint8_t radius,uint8_t x,uint8_t y) BANKED;
/* -1: ordinary puzzle/combat target; 0: environmental action refused; 1: queued. */
int8_t elements_environment(uint8_t family,uint8_t effect,uint8_t cell,uint8_t radius) BANKED;
#endif
