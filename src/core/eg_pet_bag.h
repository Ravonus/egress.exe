#ifndef EG_PET_BAG_H
#define EG_PET_BAG_H
#include "eg_bag.h"
#include "eg_pet.h"
#define EG_PET_UNIT_BYTES 3u
#define EG_PET_BAG_BYTES (EG_BAG_CAP*EG_PET_UNIT_BYTES)
#define EG_PET_ENTRY_BYTES (18u+EG_PET_BAG_BYTES)
#define EG_PET_RECORD_BYTES 112u
#define EG_PET_RECORD_VERSION 1u
#define PM_HP 31u
#define PM_REVIVED 32u
/* A carried critter retains health, its spent revival, cooldown and identity.
 * Duplicate species are distinct individuals. Re-equipping never refreshes them. */
void eg_pet_bag_put(uint8_t *meta,uint8_t slot,uint8_t hp,uint8_t spent,uint8_t cooldown,uint8_t instance) EG_BANKED;
void eg_pet_bag_remove(uint8_t *meta,uint8_t count_before,uint8_t slot) EG_BANKED;
uint8_t eg_pet_instance_next(uint8_t cursor,uint8_t active,const uint8_t *current,uint8_t ncurrent,const uint8_t *entry,uint8_t nentry,uint8_t entry_active) EG_BANKED;
void eg_pet_record_pack(uint8_t *out,const uint8_t *entry,uint16_t seed,uint8_t floor,uint8_t serial) EG_BANKED;
uint8_t eg_pet_record_valid(const uint8_t *record,const uint8_t *lo,const uint8_t *hi,uint8_t n,uint16_t seed,uint8_t floor,uint8_t serial) EG_BANKED;
#endif
