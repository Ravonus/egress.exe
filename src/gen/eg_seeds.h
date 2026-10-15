#ifndef EG_SEEDS_H
#define EG_SEEDS_H
#include <stdint.h>
#include "../core/eg_plan.h"
#define EG_TIERS 6u
#define EG_SEEDS_PER_TIER 128u
/* the k-th accepted seed of a tier (tools/bake_seeds.sh); the planner reads whole records (eg_seed_get) */
uint16_t eg_seed_pick(uint8_t tier, uint8_t k) EG_BANKED;
#endif
