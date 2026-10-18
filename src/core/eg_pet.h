#ifndef EG_PET_H
#define EG_PET_H
#include "eg_catalogue.h"
enum {PR_ATTACK,PR_HEAL,PR_SUPPORT,PR_TANK,PR_DOT,PR_TACTIC,PR_COUNT};
enum {PP_IDLE,PP_FOLLOW,PP_CAST,PP_REST,PP_HURT,PP_GUARD,PP_HEAL,PP_DEATH,PP_COUNT};
enum {PA_STRIKE=1,PA_BURN,PA_HEAL,PA_CLEANSE,PA_HASTE,PA_PUSH,PA_TAUNT,PA_BRACE,
      PA_SHOCK,PA_CHAIN,PA_SLOW,PA_FREEZE,PA_GUARD,PA_BURN_FIELD,PA_WARD,PA_BLIND,
      PA_REGEN,PA_ROOT,PA_EXPOSE,PA_REFLECT,PA_COUNT};
#define PB_HASTE 1u
#define PB_WARD 2u
#define PB_MIGHT 4u
#define PB_REGEN 8u
#define PB_REFLECT 16u
#define PB_RETALIATE 32u
#define PB_RISING 128u
#define PT_SECOND_SPRING 1u
#define PET_REVIVE_SPENT 128u /* state: phase bits 0..2, facing 3..5, buffer 6, spent charge 7 */
/* Both live state and floor-entry copy are exactly sixteen bytes. Timers in
 * this module advance once per eight simulation frames, independently of art. */
typedef struct {
 eg_item_id id;int8_t ox,oy;uint8_t cd,timer,state,shown;
 uint8_t hp,guard,buff,buff_t,threat,threat_t,auto_cd,invuln;
} eg_pet_state;
typedef struct {uint8_t role,automatic,command,hp,armour,auto_cd,command_cd,auto_cost,command_cost,range,traits;} eg_pet_profile;
uint8_t eg_pet_get(eg_item_id id,eg_pet_profile *out) EG_BANKED;
void eg_pet_start(eg_pet_state *s,eg_item_id id) EG_BANKED;
void eg_pet_tick(eg_pet_state *s) EG_BANKED;
uint8_t eg_pet_ready(const eg_pet_state *s,uint8_t manual,uint8_t stamina) EG_BANKED;
uint8_t eg_pet_commit(eg_pet_state *s,uint8_t manual) EG_BANKED;
uint8_t eg_pet_hurt(eg_pet_state *s,uint8_t damage) EG_BANKED;
void eg_pet_ability_name(eg_item_id id,uint8_t manual,char *out) EG_BANKED;
#endif
