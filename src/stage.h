#ifndef STAGE_H
#define STAGE_H
/* B2 stage types on the cartridge (src/stage.c): the run's deck and slots (core/eg_stage.h), the stage room, and how
 * each stage plays out on the floor (holdout, chase, hunt, escort, mix under pressure, dark, heist), plus the rest
 * landing (src/stage_rest.c). play.c calls the hooks below; nothing here touches a puzzle floor. Globals the PyBoy
 * harness reads or pokes are plain. */
#include <gb/gb.h>
#include <stdint.h>
#include "core/eg_stage.h"

extern uint8_t stage_type;   /* SG_*: what this floor plays (SG_PUZZLE: a B1 puzzle room) */
extern uint8_t stage_kind;   /* SK_STAGE, SK_MIDBOSS, SK_BOSS */
extern uint8_t dbg_stage;    /* harness: nonzero forces stage type dbg_stage - 1 on every generated floor */
extern eg_stage stg;
extern eg_slice slice;
extern char stage_goal[19];  /* the objective as shown (the harness reads it) */
extern char stage_hud[7];    /* the HUD's cols 10..15 on a stage floor (6 characters; "" shows the keycards) */
extern uint8_t stage_won;    /* 1: the objective is met (the door opened for it) */
extern uint8_t stage_lost;   /* 1: the stage is lost without a death (the temp is gone): hold SELECT */
extern uint8_t stage_wave;   /* waves begun */
extern uint8_t stage_pct;    /* holdout: the egress's charge, % */
extern uint8_t stage_row;    /* chase: the row dropping now (the front); 255 before it starts */
extern uint8_t stage_cell;   /* hunt: the target's en[] index; escort: the temp's cell */
extern uint8_t stage_thp;    /* escort: the temp's hit points */

void stage_run_begin(void) BANKED;   /* play_run: a new run's deck */
void stage_plan_floor(void) BANKED;  /* before floor_build: the slot, the type, floor_seed and floor_tier */
void stage_resume_floor(void) BANKED; /* restore constructor seed without dealing the saved floor a second time */
uint8_t stage_build(void) BANKED;    /* in floor_build: 1 if the floor is a stage room (else build the puzzle room) */
void stage_start(void) BANKED;       /* the end of floor_build (a reset too): the stage's own state */
void stage_show(void) BANKED;        /* floor_show, display off, after look_load: cards, skins, palette */
uint8_t stage_announce(void) BANKED; /* the floor's first toast: 1 if the stage showed its objective */
void stage_tick(void) BANKED;        /* every frame of play, after player_update */
uint8_t stage_select(void) BANKED;   /* a tap of SELECT: 1 if the stage used it (the heist's desk, mixp's ring) */
void stage_floor_done(void) BANKED;  /* through the door: the planner's result for a planned puzzle floor */
void stage_landing(void) BANKED;     /* the landing between segments, then the rest (src/stage_rest.c) */

#endif
