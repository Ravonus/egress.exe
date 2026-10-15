/* Player builds and the abstract combat model (host sim). */
#ifndef MX_BUILD_H
#define MX_BUILD_H
#include "mx.h"

enum { WB_BLADE, WB_HAMMER, WB_SPEAR, WB_DISC, WB_WAND, WB_GAUNTLET, WB_COUNT };
enum { DG_ROLL, DG_BLINK, DG_SLIDE, DG_COUNT };
#define N_MODS 18
#define N_TRAITS 14

typedef struct { const char *name; uint8_t dps, aoe, reach, parry, pierce, push, proj; mx_piece p; } mx_wbase;
typedef struct { const char *name; mx_piece up, cost; const char *cost_text; } mx_trait;
typedef struct { const char *name; uint8_t avoid; mx_piece p; const char *text; } mx_dodge;
typedef struct { uint8_t base, mod[2], trait[2], dodge; } mx_build;

extern const mx_wbase mx_wbases[WB_COUNT];
extern const mx_named_piece mx_mods[N_MODS];
extern const mx_trait mx_traits[N_TRAITS];
extern const mx_dodge mx_dodges[DG_COUNT];

#define ARCH_HYBRID MX_TAGS
#define ARCH_PLAIN (MX_TAGS + 1)
#define ARCH_COUNT (MX_TAGS + 2)

typedef struct {
  uint16_t tags;
  uint8_t tagmag[MX_TAGS];
  int16_t off, def, ctrl, avoid, hp, heal, aoe, pierce, push, parry, proj, armorbreak;
  uint8_t arch, mono;
  uint16_t grp[SG_COUNT]; /* stacked group values, for reporting */
  /* trait costs that act per room, not through the stacks (bred heirs) */
  uint8_t el_mult[MX_TAGS];   /* x16: damage taken from foes of each element (COLOURBLIND, ALLERGIC, DAMP) */
  uint8_t st_mult[ST_COUNT];  /* x16: damage taken per stage type (LOUD) */
  uint8_t mix_pen;            /* REMEMBERS: extra recipe steps in mix rooms (a forgotten recipe) */
  uint8_t foe_pct;            /* ECHO: % more enemy output (the ghost's killer spawns) */
} mx_bstats;

void mx_build_stats(const mx_build *b, mx_bstats *s);
void mx_build_stats_ex(const mx_build *b, const mx_piece *extra, uint8_t nextra, mx_bstats *s); /* + extra pieces (bred heirs) */
/* 1 = cleared the run. *depth_out = floor reached. K = enemy damage scale x256 */
uint8_t mx_sim_run(const mx_build *b, const mx_bstats *s, const mx_run *run, uint32_t seed, uint16_t K, uint8_t *depth_out);
extern const char *const mx_arch_name[ARCH_COUNT];
extern uint8_t mx_heal_cap; /* HP healed per room from lifesteal pieces (tunable cap) */
#endif
