#ifndef EG_CATALOGUE_H
#define EG_CATALOGUE_H
/* Stable nine-bit discovery identities. The room solver's sixteen effect
 * primitives stay independent of catalogue size. No allocation or runtime RNG. */
#include "eg_room.h"
#define EG_DISCOVERIES 444u
#define EG_DISCOVERY_BYTES 56u
#define EG_FAMILIES 24u
#define EG_FORMS 16u
#define EG_BOMBS_FIRST 385u
#define EG_TOOLS_FIRST 409u
#define EG_PETS_FIRST 433u
#define CC_HOT 1u
#define CC_COLD 2u
#define CC_POWER 4u
#define CC_MUD 8u
#define CC_WEIGHT 16u
enum {CK_MATERIAL,CK_BOMB,CK_TOOL,CK_PET};
enum {CS_TOUCH,CS_LINE,CS_BURST,CS_FAN,CS_CHAIN,CS_FIELD,CS_WARD,CS_AURA};
typedef uint16_t eg_item_id;
typedef struct {uint8_t family,effect,kind,shape,power,radius,cost;} eg_item_profile;
uint8_t eg_catalogue_family(eg_item_id id) EG_BANKED;
uint8_t eg_catalogue_form(eg_item_id id) EG_BANKED;
uint8_t eg_catalogue_capabilities(const uint8_t *family_counts24) EG_BANKED;
void eg_catalogue_profile(eg_item_id id,eg_item_profile *out) EG_BANKED;
eg_item_id eg_catalogue_variant(uint8_t family,uint8_t form) EG_BANKED;
eg_item_id eg_catalogue_recipe(eg_item_id a,eg_item_id b) EG_BANKED;
uint8_t eg_catalogue_ingredients(eg_item_id id,eg_item_id *a,eg_item_id *b) EG_BANKED;
void eg_catalogue_name(eg_item_id id,char *out) EG_BANKED; /* <=18 chars plus NUL */
void eg_catalogue_abbr(eg_item_id id,char *out) EG_BANKED; /* 4 chars plus NUL */
void eg_catalogue_hint(eg_item_id id,char *out) EG_BANKED; /* <=18 chars plus NUL */
uint8_t eg_discovery_has(const uint8_t *bits,eg_item_id id) EG_BANKED;
uint8_t eg_discovery_mark(uint8_t *bits,eg_item_id id) EG_BANKED;
uint16_t eg_discovery_count(const uint8_t *bits) EG_BANKED;
#endif
