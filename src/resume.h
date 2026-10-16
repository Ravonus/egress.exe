#ifndef EG_RESUME_H
#define EG_RESUME_H
#include <gb/gb.h>
enum { RES_NONE=0, RES_FLOOR=1, RES_LANDING=2, RES_DEAD=3, RES_WON=4 };
extern uint8_t resume_ready,resume_state,resume_rest,resume_new;
uint8_t resume_probe(void) BANKED;
uint8_t resume_restore(void) BANKED;
uint8_t resume_finish_pending(void) BANKED;
void resume_save(uint8_t state) BANKED;
void resume_damage(void) BANKED;
void resume_clear(void) BANKED;
#endif
