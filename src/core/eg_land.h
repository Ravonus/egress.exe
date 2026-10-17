#ifndef EG_LAND_H
#define EG_LAND_H
#include "eg_room.h"
/* Authored rules select the problems; the seed constructs their geography.
 * Global coordinates are explicit bytes, never local EG_IDX values. Querying a
 * cell is independent of traversal order and needs no full-map allocation. */
#define EL_SIZES 6u
#define EL_RULES 6u
#define EL_HAZARDS 128u /* versioned geography for newly dealt runs */
#define EL_BIOMES 8u
#define EL_NODES 4u
#define EL_ITEMS 16u
#define EL_PLAN_BYTES (11u+6u*EL_NODES+3u*EL_ITEMS)
enum { EL_XS,EL_S,EL_M,EL_L,EL_XL,EL_XXL };
enum { EL_LOAD,EL_HEAT,EL_COLD,EL_POWER,EL_PORTAL,EL_GOALS };
enum { EL_HOLD,EL_LATCH,EL_EITHER,EL_BOTH };
enum { EL_TERRACES,EL_BASINS,EL_BEND,EL_COURTS,EL_SHAPES };
#define EL_PUSH 1u
#define EL_CRAFT 2u
#define EL_JUMP 4u
enum { EL_FLOOR,EL_FERN,EL_FLOWER,EL_REED,EL_PROP,EL_RIVER,EL_FALL,EL_PAD,EL_DESIGNS };
#define EL_PROTECTED 1u
typedef struct {uint8_t kind,at,lane,condition,needs,alternatives;} eg_land_node;
typedef struct {uint8_t u,v,piece;} eg_land_item;
typedef struct {
  uint16_t seed;
  uint8_t width,height,size,biome,rule,shape,vertical,nnode,nitem;
  eg_land_node node[EL_NODES];
  eg_land_item item[EL_ITEMS];
} eg_land;
typedef struct {uint8_t material,design,variant,flags;} eg_land_cell;
/* Invalid IDs, an incompatible explicit size or an invalid output pointer fail
 * without modifying output. Size 255 lets the codec deal an allowed profile. */
uint8_t eg_land_plan(eg_land *out,uint16_t seed,uint8_t rule,uint8_t size,uint8_t biome) EG_BANKED;
void eg_land_at(const eg_land *p,uint8_t x,uint8_t y,eg_land_cell *out) EG_BANKED;
uint8_t eg_land_lane(const eg_land *p,uint8_t u) EG_BANKED;
uint8_t eg_land_item_at(const eg_land *p,uint8_t x,uint8_t y) EG_BANKED;
/* Canonical progression coordinates: u follows the stage, v crosses it. */
#define EL_X(p,u,v) ((p)->vertical ? (v) : (u))
#define EL_Y(p,u,v) ((p)->vertical ? (u) : (v))
#define EL_LENGTH(p) ((p)->vertical ? (p)->height : (p)->width)
#define EL_CROSS(p) ((p)->vertical ? (p)->width : (p)->height)
#endif
