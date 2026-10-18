#ifndef EG_COMPANION_H
#define EG_COMPANION_H
#include <gb/gb.h>
#include "core/eg_pet.h"
typedef eg_pet_state eg_companion;
#define CP_IDLE PP_IDLE
#define CP_FOLLOW PP_FOLLOW
#define CP_CAST PP_CAST
#define CP_REST PP_REST
#define ST_COMPANION 124u
extern eg_companion __at(0xDDDC) companion;
extern uint8_t __at(0xDDD9) pet_instance;
void companion_begin(void) BANKED;
void companion_enter(void) BANKED;
void companion_restore(void) BANKED;
uint8_t companion_equip(uint8_t slot) BANKED;
uint8_t companion_command(void) BANKED;
void companion_tick(void) BANKED;
void companion_draw(void) BANKED;
void companion_art_tick(void) BANKED;
uint8_t companion_assist(void) BANKED;
void companion_reset_position(void) BANKED;
void companion_shift_view(int8_t dx,int8_t dy) BANKED;
uint8_t companion_aim(uint8_t enemy,uint8_t *x,uint8_t *y) BANKED;
uint8_t companion_touch(uint8_t damage,uint8_t x,uint8_t y,uint8_t range) BANKED;
uint8_t companion_intercept(uint8_t x,uint8_t y) BANKED;
uint8_t companion_protect(uint8_t damage) BANKED;
#endif
