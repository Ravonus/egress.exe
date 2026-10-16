#ifndef WORLD_PLAY_H
#define WORLD_PLAY_H
#include <gb/gb.h>
extern uint16_t world_seams;
extern uint8_t world_moved;
uint8_t world_work_pending(void) BANKED;
void world_play_reset(void) BANKED;
uint8_t world_frame_begin(void) BANKED;
void world_prefetch(void) BANKED;
void world_frame_end(void) BANKED;
#endif
