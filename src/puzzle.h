#ifndef PUZZLE_H
#define PUZZLE_H
/* B1 puzzle rooms on the cartridge (src/puzzle.c): the pocket, the bench overlay, using pieces, reaction tiles. */
#include <gb/gb.h>
#include <stdint.h>
#include "core/eg_room.h"
#include "core/eg_catalogue.h"
#include "core/eg_bag.h"
#include "core/eg_pet_bag.h"

#define POCKET_CAP 24u
#define POCKET_EFFECT_CAP 29u
extern uint8_t __at(0xDBC0) pocket[POCKET_CAP]; /* finite carried units, insertion order */
extern uint8_t __at(0xDDD0) pocket_hi[3];
extern uint8_t __at(0xDDF0) pet_meta[EG_PET_BAG_BYTES];
extern uint8_t __at(0xDDDA) pet_serial;
extern uint8_t __at(0xDBBD) effect_n;
extern uint8_t pocket_n,tool_cd;
extern eg_item_id tool_piece;
extern const uint8_t look_bits[8];
/* A checked slot, without side effects. Inline reads avoid both ROM0 growth
 * and a bank switch per inventory comparison. */
#define pocket_id(slot) EG_BAG_VALUE_MASK(pocket,pocket_hi,(slot),look_bits[(slot)&7u])
uint8_t pocket_add(eg_item_id id) BANKED;
uint8_t pocket_add_pet(eg_item_id id,uint8_t hp,uint8_t spent,uint8_t cooldown,uint8_t instance) BANKED;
uint8_t pocket_pet_event(uint8_t instance,const eg_pet_state *state,uint8_t dead) BANKED;
void pocket_entry_snapshot(uint8_t *lo,uint8_t *hi,uint8_t *n,eg_item_id *tool) BANKED;
void pocket_take(uint8_t slot) BANKED;
uint8_t pocket_effects(uint8_t *out) BANKED;
extern uint8_t __at(0xDBA5) bench_mode; /* 0 satchel/equip, 1 physical crafting bench */
uint8_t puzzle_bench_interact(void) BANKED;
uint8_t pocket_count(eg_item_id piece) BANKED;
void puzzle_tool_use(void) BANKED;
void pocket_begin(void) BANKED;
void pocket_enter(void) BANKED;
void pocket_restore(void) BANKED;
extern uint8_t bench_on, bench_cur, last_rx;
void pocket_reset(void) BANKED;
void puzzle_shift_view(int8_t dx,int8_t dy) BANKED;
void puzzle_pick(uint8_t cell) BANKED; /* the player walked into a T_ITEM cell */
uint8_t puzzle_meta(uint8_t i) BANKED;    /* the card a cell of type T_CRUMBLE and up shows */
void bench_run(void) BANKED;           /* shared window: inventory from menu, crafting only through physical interaction */

/* fragile floor, the dead-room hint and the run planner */
#include "core/eg_plan.h"
extern uint8_t puzzle_dirty;  /* set after a break or a push: puzzle_tick looks for a dead room */
extern uint8_t frag_skip;     /* B3: a blink sets it, and the step it makes doesn't wear the floor */
extern uint8_t floor_resets;
extern eg_plan_state plan;
void puzzle_tick(void) BANKED;
uint8_t puzzle_frag_hit(uint8_t cell, uint8_t who) BANKED; /* B2 mobs: break fragile floor (refused if protected) */
void puzzle_break_fx(uint8_t cell, uint8_t kind) BANKED;    /* a fragile cell broke: b1-look's per-tile break */
void puzzle_plan_begin(void) BANKED;
void puzzle_plan_floor(void) BANKED; /* sets floor_seed and floor_tier */
void puzzle_plan_done(void) BANKED;
void puzzle_reset_cost(void) BANKED;

#endif
