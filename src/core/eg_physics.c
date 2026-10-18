#ifdef __SDCC
#pragma bank 255
#endif
#include <string.h>
#include "eg_physics.h"
#include "eg_piece.h"

void eg_physics_gen(eg_room *r, uint8_t motif, uint16_t seed, uint8_t level) EG_BANKED {
  uint8_t x, y, c, wall, px, doorx, row;
  if(motif==PH_PORTAL) {eg_portal_gen(r,seed,level);eg_health_room(r,seed);return;}
  memset(r, 0, sizeof *r);
  eg_rs = seed ? seed : 1u;
  for (y = 0; y != EG_H; y++)
    for (x = 0; x != EG_W; x++)
      r->c[EG_IDX(x,y)] = (!x || !y || x == EG_W-1u || y == EG_H-1u) ? T_WALL : T_EMPTY;
  wall = (uint8_t)(4u + eg_rndn(3u));
  px = (uint8_t)(4u + eg_rndn(7u));
  doorx = (uint8_t)(2u + eg_rndn(12u));
  r->door = EG_IDX(doorx, 0);
  r->c[r->door] = T_DOOR;
  r->start = EG_IDX(2u,12u);
  r->keys = 1u;
  r->trick = TRICK_WEIGHT; /* physical plates, not a new random-generator trick ID */
  r->c[EG_IDX(doorx,2u)] = T_KEY;
  if (motif == PH_BRIDGE) {
    px = (uint8_t)(7u + eg_rndn(2u));
    row = (uint8_t)(5u + eg_rndn(5u));
    r->door = EG_IDX(12u,0u);
    r->c[EG_IDX(doorx,0u)] = T_WALL;
    r->c[r->door] = T_DOOR;
    r->c[EG_IDX(doorx,2u)] = T_EMPTY;
    r->c[EG_IDX(12u,2u)] = T_KEY;
    for (y = 1u; y != EG_H-1u; y++) r->c[EG_IDX(px,y)] = T_PIT;
    r->c[EG_IDX(px-2u,row)] = T_ROCK;
    r->c[EG_IDX(px-2u,row+1u)] = T_WALL;
    r->c[EG_IDX(px-1u,row+1u)] = T_WALL;
    /* A scarce crafted crossing and a free physical crossing are independently viable. */
    r->nitem = 2u;
    r->item_cell[0] = EG_IDX(3u,11u); r->item_piece[0] = P_EARTH;
    r->item_cell[1] = EG_IDX(4u,11u); r->item_piece[1] = P_WATER;
    for (c = 0; c != 2u; c++) r->c[r->item_cell[c]] = T_ITEM;
  } else {
    for (x = 1u; x != EG_W-1u; x++) r->c[EG_IDX(x,wall)] = T_WALL;
    r->c[EG_IDX(doorx,wall)] = T_GATE;
    if (motif == PH_DROP) {
      r->c[EG_IDX(px,wall+2u)] = T_ROCK;
      r->c[EG_IDX(px,wall+3u)] = T_DIRT;
      r->c[EG_IDX(px,11u)] = T_PLATE;
      r->c[EG_IDX(px,12u)] = T_WALL; /* a real pedestal supports the load above the open edge */
      /* A chute reads as a structure and stops the load rolling off the switch. */
      for (y = (uint8_t)(wall+4u); y != 13u; y++) {
        r->c[EG_IDX(px-1u,y)] = T_WALL;
        r->c[EG_IDX(px+1u,y)] = T_WALL;
      }
    } else {
      r->c[EG_IDX(px,wall+2u)] = T_PLATE;
      r->c[EG_IDX(px+(seed&1u ? 1u : 0u),wall+4u)] = T_BLOCK;
      if (level >= 6u) {
        /* The second load needs another push direction, not just a longer hallway. */
        r->c[EG_IDX(12u,wall+2u)] = T_PLATE;
        r->c[EG_IDX(11u,wall+3u)] = T_BLOCK;
      }
    }
  }
  /* A visible cracked partition guards an optional token cache. */
  r->c[EG_IDX(13u,10u)] = T_WALL;
  r->c[EG_IDX(12u,11u)] = T_BRICK;
  r->c[EG_IDX(13u,11u)] = T_TOKEN;
  r->c[EG_IDX(14u,11u)] = T_WALL;
  r->c[EG_IDX(13u,12u)] = T_WALL;
  r->tokens = 1u;
  if (motif != PH_BRIDGE) {
    r->nitem = motif==PH_DROP ? 3u : 2u;
    r->item_cell[0] = EG_IDX(2u,11u); r->item_cell[1] = EG_IDX(3u,11u);
    r->item_piece[0] = motif == PH_DROP ? P_METAL : P_EARTH;
    r->item_piece[1] = motif == PH_DROP ? P_AIR : P_WATER;
    /* The chute's switch can occupy (4,11); keep the third reagent in the
     * clear approach instead of replacing an apparatus cell for that seed. */
    if(motif==PH_DROP){r->item_cell[2]=EG_IDX(2u,10u);r->item_piece[2]=P_FIRE;r->c[r->item_cell[2]]=T_ITEM;}
    r->c[r->item_cell[0]] = r->c[r->item_cell[1]] = T_ITEM;
  }
  eg_island_edges(r);
  eg_health_room(r,seed);
}

void eg_physics_goal(uint8_t motif, char *out) EG_BANKED {
  const char *s = motif == PH_PORTAL ? "POWER. CROSS. BURN." : motif == PH_DROP ? "DIG. RETREAT. DROP." :
                  motif == PH_BRIDGE ? "CRT OR MUD: CROSS" : "PUSH ONTO THE PLATE";
  while (*s) *out++ = *s++;
  *out = 0;
}
