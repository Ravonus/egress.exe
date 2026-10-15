#ifndef EG_SEEDS_H
#define EG_SEEDS_H
#include <gb/gb.h>
#include <stdint.h>
#define EG_TIERS 6u
#define EG_SEEDS_PER_TIER 128u
/* the k-th accepted seed of a tier (tools/bake_seeds.sh) */
uint16_t eg_seed_pick(uint8_t tier, uint8_t k) BANKED;
#endif
