#ifndef TUTORIAL_H
#define TUTORIAL_H
#include <gb/gb.h>
enum { TE_PUSH,TE_PLATE,TE_A,TE_B,TE_BENCH,TE_EVADE,TE_STAMINA,TE_PORTAL,TE_COUNT };
extern uint8_t tutorial_on;
extern uint16_t tutorial_seen;
void tutorial_init(void) BANKED;
void tutorial_save(void) BANKED;
void tutorial_queue(uint8_t event) BANKED;
void tutorial_tick(void) BANKED;
#endif
