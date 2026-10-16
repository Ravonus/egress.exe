#ifndef EG_FAMILY_H
#define EG_FAMILY_H
#include "eg_room.h"
#define EG_FAMILY_HISTORY 8u
enum { G_SKIN, G_LOOK, G_HAIR, G_NECK, G_PATTERN, G_MARK, G_ACCENT, G_LOOKS };
enum { G_VIGOR, G_PACE, G_POWER, G_FOCUS, G_STATS };
typedef struct {
  uint8_t look[G_LOOKS], weapon[2], dodge[2], traits[2], stat[G_STATS];
  uint16_t seed;
} eg_genome;
typedef struct { eg_genome gene; uint16_t generation; uint8_t floor, won; } eg_ancestor;
typedef struct {
  eg_genome current, parent, child[3];
  eg_ancestor history[EG_FAMILY_HISTORY]; /* most recent actual player first */
  uint16_t generation, rng;
  uint8_t active, nrecord, selected;
  uint16_t seen; /* full compact curriculum, including the four living-obstacle lessons */
} eg_family;
void eg_founder(eg_genome *g, uint16_t seed) EG_BANKED;
void eg_children(const eg_genome *parent, uint16_t seed, eg_genome *out3) EG_BANKED;
void eg_sibling_style(const eg_genome *parent,eg_genome *child,uint8_t k) EG_BANKED;
void eg_family_init(eg_family *f, uint16_t seed) EG_BANKED;
void eg_family_choose(eg_family *f, uint8_t choice) EG_BANKED;
void eg_family_finish(eg_family *f, uint8_t floor, uint8_t won) EG_BANKED;
uint8_t eg_gene_dodge(const eg_genome *g) EG_BANKED;
#endif
