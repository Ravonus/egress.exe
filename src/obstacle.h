#ifndef OBSTACLE_H
#define OBSTACLE_H
#include "play.h"
#include "core/eg_obst.h"
#define EG_OB_TICK 24u
extern eg_ob obstacle[EG_MAX_OB];
extern uint8_t obstacle_phase, obstacle_clock;
void obstacle_reset(void) BANKED;
void obstacle_show(void) BANKED;
void obstacle_tick(void) BANKED;
void obstacle_draw(void) BANKED;
void obstacle_noise(uint8_t cell) BANKED;
void obstacle_hit(uint8_t x, uint8_t y, uint8_t rx, uint8_t ry) BANKED;
#endif
