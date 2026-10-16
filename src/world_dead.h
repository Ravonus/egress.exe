#ifndef WORLD_DEAD_H
#define WORLD_DEAD_H
#include <gb/gb.h>
void world_dead_begin(void) NONBANKED;
uint8_t world_dead_step(uint8_t from,uint8_t budget) NONBANKED;
#endif
