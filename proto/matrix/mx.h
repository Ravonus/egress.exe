/* EGRESS.EXE "super matrix" prototype: shared types and tables.
 * Design-stage proof. C99, integer only, no malloc, deterministic.
 * Written in the SDCC style (uint8_t/int16_t, small const tables) so the
 * core can move into src/ later; the host-only sim code lives in sim.c and
 * mx_build.c and may use wider accumulators. */
#ifndef MX_H
#define MX_H
#include <stdint.h>

/* ---------- tags ---------- */
enum {
  MX_HOT, MX_COLD, MX_WET, MX_SHOCK, MX_SHINY, MX_ALIVE, MX_MAGIC, MX_GLITCH,
  MX_STONE, MX_AIRY, MX_TAGS, MX_NOTAG = 15
};
#define TB(t) ((uint16_t)(1u << (t)))
/* means bits above the tags: capabilities a kill/win condition can need */
#define M_CRUSH  ((uint16_t)(1u << 10)) /* crate/boulder or pit to drop into */
#define M_PROJ   ((uint16_t)(1u << 11)) /* an enemy projectile exists to reflect */
#define M_LIGHT  ((uint16_t)(1u << 12)) /* lamp, brazier, or HOT/SHINY source */
#define M_TAGMASK ((uint16_t)0x03FF)

/* ---------- piece schema ---------- */
enum {
  TR_HIT, TR_DODGE, TR_PARRY, TR_KILL, TR_CHARGE, TR_LOWHP, TR_PUSH, TR_RECIPE,
  TR_EVERY, TR_HURT, TR_TILE, TR_PASSIVE, TR_COUNT
};
enum {
  EF_PROJ, EF_AREA, EF_STATUS, EF_TERRAIN, EF_STAT, EF_SUMMON, EF_TRAIL, EF_BEAM, EF_COUNT
};
enum {
  PK_WBASE, PK_WMOD, PK_TRAIT, PK_DODGE, PK_MOBPART, PK_AFFIX, PK_BOSSPART, PK_ELEMENT, PK_COUNT
};
enum {
  SG_DAMAGE, SG_STATUS, SG_AREA, SG_PROJ, SG_DEFENSE, SG_SPEED, SG_SUMMON, SG_REACT, SG_COUNT
};
/* cap class 3 marks a COST (negative) rule; 0..2 pick soft-cap tightness */
#define CAP_COST 3

/* 5 bytes per piece:
 *  b0 = trigger:4 | effect:4
 *  b1 = tagA:4    | tagB:4      (15 = none)
 *  b2 = magnitude (0..255, percent-ish units)
 *  b3 = argA:4    | argB:4      (count/period/radius | duration in 1/4 s)
 *  b4 = kind:3    | group:3 | cap:2 */
typedef struct { uint8_t b[5]; } mx_piece;
#define MXP(tr, ef, ta, tb, mag, aa, ab, kind, grp, cap) \
  {{ (uint8_t)(((tr) << 4) | (ef)), (uint8_t)(((ta) << 4) | (tb)), (uint8_t)(mag), \
     (uint8_t)(((aa) << 4) | (ab)), (uint8_t)(((kind) << 5) | ((grp) << 2) | (cap)) }}
#define P_TRIG(p)  ((uint8_t)((p)->b[0] >> 4))
#define P_EFF(p)   ((uint8_t)((p)->b[0] & 15))
#define P_TAGA(p)  ((uint8_t)((p)->b[1] >> 4))
#define P_TAGB(p)  ((uint8_t)((p)->b[1] & 15))
#define P_MAG(p)   ((p)->b[2])
#define P_ARGA(p)  ((uint8_t)((p)->b[3] >> 4))
#define P_ARGB(p)  ((uint8_t)((p)->b[3] & 15))
#define P_KIND(p)  ((uint8_t)((p)->b[4] >> 5))
#define P_GROUP(p) ((uint8_t)(((p)->b[4] >> 2) & 7))
#define P_CAP(p)   ((uint8_t)((p)->b[4] & 3))

typedef struct { const char *name; mx_piece p; } mx_named_piece;

extern const char *const mx_tag_name[MX_TAGS];
extern const char *const mx_trig_name[TR_COUNT];
extern const char *const mx_eff_name[EF_COUNT];
extern const char *const mx_kind_name[PK_COUNT];
extern const char *const mx_group_name[SG_COUNT];

/* stacking: sorted, diminishing (16,12,8,6,4,3,2,2 /16), soft knee at cap, hard at cap*3/2 */
extern uint8_t mx_cap[SG_COUNT];   /* tunable soft caps (const in game) */
extern uint8_t mx_dr_on;           /* 0 = linear uncapped (for the "before tuning" sim) */
uint16_t mx_stack(const uint8_t *mag, uint8_t n, uint8_t group);

/* ---------- reactions ---------- */
enum { /* combat result kinds */
  RK_NONE, RK_BURST, RK_DOT, RK_BLIND, RK_STUN, RK_ROOT, RK_SLOW, RK_SPLIT, RK_REFLECT,
  RK_SUMMON, RK_ARMOR, RK_PUSH, RK_REVEAL, RK_DISPEL, RK_NULL, RK_RANDOM, RK_LIGHT,
  RK_HEAL, RK_CHAIN, RK_CONFUSE, RK_PULL, RK_WALL, RK_COUNT
};
enum { /* tiles; each has an intrinsic tag so tiles react like mobs */
  TL_FLOOR, TL_WATER, TL_ICE, TL_PIT, TL_WALL, TL_CRACK, TL_VINE, TL_MUD, TL_LAVA,
  TL_MIRROR, TL_VENT, TL_NODE, TL_CRATE, TL_LAMP, TL_RIFT, TL_STEAM, TL_BRIDGE,
  TL_SNOW, TL_LIGHT, TL_TURRET, TL_COUNT, TL_KEEP = 255
};
typedef struct {
  const char *name;
  uint8_t kind;    /* RK_ */
  uint8_t mult;    /* damage multiplier x16 on the triggering hit (16 = none) */
  uint8_t ctrl;    /* % enemy output removed while it lasts */
  uint8_t dur;     /* quarter seconds */
  uint8_t tile_in; /* tile it converts (TL_FLOOR = any open tile) */
  uint8_t tile_out;
  const char *tile_note;
} mx_reaction;
enum { RX_NONE = 0, RX_REFLECT = 48, RX_COUNT = 49 };
extern const mx_reaction mx_rx[RX_COUNT];
extern const uint8_t mx_rx_table[MX_TAGS][MX_TAGS];
extern const uint8_t mx_tile_tag[TL_COUNT];
extern const char *const mx_tile_name[TL_COUNT];
#define CTX_PROJ 1
/* resolve: returns primary reaction; *extra gets REFLECT when SHINY meets a projectile */
uint8_t mx_react(uint8_t a, uint8_t b, uint8_t ctx, uint16_t seed, uint8_t *extra);
uint8_t mx_tile_react(uint8_t tile, uint8_t tag, uint16_t seed, uint8_t *rx_out);

/* ---------- rng ---------- */
typedef struct { uint32_t s; } mx_rng;
void mx_seed(mx_rng *r, uint32_t seed);
uint16_t mx_rand(mx_rng *r);
uint8_t mx_below(mx_rng *r, uint8_t n);
uint8_t mx_pick(mx_rng *r, const uint8_t *w, uint8_t n);

/* ---------- mobs ---------- */
enum { BD_WALKER, BD_FLYER, BD_PHASER, BD_BURROWER, BD_CRAWLER, BD_GRAVITY, BD_HEAVY,
       BD_TELEPORT, BD_SWARM, BD_TURRET, BD_MIMIC, BD_COUNT };
enum { AT_LUNGE, AT_PROJ, AT_SLAM, AT_BEAM, AT_TRAIL, AT_SUMMON, AT_GRAB, AT_TERRA, AT_COUNT };
enum { DF_PLAIN, DF_ARMOR, DF_WETSHOCK, DF_CRUSH, DF_SPLITS, DF_REFORM, DF_REFLECT, DF_DARK, DF_COUNT };
enum { BH_COWARD, BH_AGGRO, BH_FLANK, BH_GUARD, BH_LEADER, BH_THIEF, BH_COUNT };
#define EL_COUNT (MX_TAGS + 1) /* element index MX_TAGS = untagged */
enum { AF_SHIELD, AF_VAMP, AF_SPLIT, AF_HASTE, AF_TPHIT, AF_COLLAPSE, AF_AURA0, AF_COUNT = AF_AURA0 + MX_TAGS };
#define AF_NONE 255

typedef struct { uint8_t body, atk, def, el, beh; uint8_t af[2]; } mx_mob;
extern const char *const mx_body_name[BD_COUNT];
extern const char *const mx_atk_name[AT_COUNT];
extern const char *const mx_def_name[DF_COUNT];
extern const char *const mx_beh_name[BH_COUNT];
extern const char *const mx_af_name[AF_AURA0];
extern const uint8_t mx_atk_tell[AT_COUNT];  /* frames at 60 Hz */
extern const uint8_t mx_atk_lit[AT_COUNT];   /* tell visible/audible in the dark */
extern const char *const mx_def_rule[DF_COUNT];
extern const char *const mx_af_rule[AF_AURA0];

uint8_t mx_mob_valid(const mx_mob *m);          /* base compat rules */
uint8_t mx_affix_ok(const mx_mob *m, uint8_t a);/* single affix vs mob */
uint8_t mx_elite_valid(const mx_mob *m);        /* with af[] filled */
uint16_t mx_mob_means(const mx_mob *m);         /* means bits needed to kill it */
uint16_t mx_mob_self_means(const mx_mob *m);    /* means it supplies itself */
uint32_t mx_mob_id(const mx_mob *m);            /* base id, 17 bits */
uint8_t mx_mob_cost(const mx_mob *m);
void mx_mob_name(const mx_mob *m, uint8_t biome, char *out); /* <= 13 chars */

/* bosses */
enum { BC_COLOSSUS, BC_HIVE, BC_ENGINE, BC_SPECTER, BC_COUNT };
enum { HZ_RING, HZ_FLOOD, HZ_DARK, HZ_CONVEYOR, HZ_DEBRIS, HZ_LAVA, HZ_COUNT };
typedef struct { uint8_t core, atk[3], weak[3], hazard, el[2]; } mx_boss;
extern const char *const mx_core_name[BC_COUNT];
extern const char *const mx_hz_name[HZ_COUNT];
uint8_t mx_boss_valid(const mx_boss *b);
uint16_t mx_boss_means(const mx_boss *b);

/* ---------- world ---------- */
enum { BI_OFFICE, BI_VENTS, BI_ATRIUM, BI_SERVER, BI_MALL, BI_DREAM, BI_ROOF, BI_GLITCH, BI_COUNT };
enum { ST_PUZZLE, ST_HOLDOUT, ST_ESCORT, ST_CHASE, ST_HUNT, ST_MIDBOSS, ST_BOSS, ST_HEIST,
       ST_DARK, ST_MIX, ST_REST, ST_RIFT, ST_COUNT };
enum { FC_COMPILE, FC_CHARGE, FC_SKY, FC_WATCH, FC_ROOT, FC_LOCK, FC_COUNT };
enum { LN_WORK, LN_DREAM, LN_ESCAPE, LN_GLITCH, LN_COUNT }; /* story leanings */
enum { MD_NONE, MD_FLOODED, MD_DARK, MD_WINDY, MD_OVERGROWN, MD_GLITCHY, MD_HEAT, MD_COLLAPSE, MD_COUNT };
extern const char *const mx_md_name[MD_COUNT];
typedef struct { const char *name; uint16_t need; uint8_t steps; } mx_recipe;
#define MX_RECIPES 16
extern const mx_recipe mx_recipes[MX_RECIPES];
extern const char *const mx_biome_name[BI_COUNT];
extern const char *const mx_stage_name[ST_COUNT];
extern const char *const mx_stage_win[ST_COUNT];
extern const char *const mx_fc_name[FC_COUNT];
extern const char *const mx_ln_name[LN_COUNT];
extern const uint8_t mx_biome_body_w[BI_COUNT][BD_COUNT];
extern const uint8_t mx_biome_atk_w[BI_COUNT][AT_COUNT];
extern const uint8_t mx_biome_def_w[BI_COUNT][DF_COUNT];
extern const uint8_t mx_biome_el_w[BI_COUNT][EL_COUNT];
extern const uint8_t mx_biome_beh_w[BI_COUNT][BH_COUNT];
extern const uint8_t mx_biome_stage_w[BI_COUNT][ST_COUNT];
extern const uint8_t mx_biome_tile_w[BI_COUNT][TL_COUNT];
extern const uint8_t mx_fc_tags[FC_COUNT][2];
extern const uint8_t mx_biome_fc[BI_COUNT];

#define RUN_BIOMES 5
#define FLOORS_PER_BIOME 2
#define RUN_FLOORS (RUN_BIOMES * FLOORS_PER_BIOME)
#define STAGES_PER_FLOOR 4
#define MAX_RUN_STAGES 64
#define MAX_STAGE_MOBS 3
#define MAX_TILES 8

/* treasure (placed after the main path; never on it) */
enum { TK_TOKENS, TK_INGREDIENT, TK_PIECE, TK_LORE, TK_RELIC, TK_COSMETIC, TK_CURSED, TK_COUNT };
enum { SP_RAISED, SP_LEDGE, SP_CRUMBLE, SP_HAZARD, SP_FLYER, SP_CARD,      /* visible-hard */
       SP_FOAM, SP_DRYWALL, SP_PATCH, SP_WATER, SP_COUNT };                  /* hidden */
#define SP_FIRST_HIDDEN SP_FOAM
enum { TG_NONE, TG_SMALL, TG_HEAVY, TG_BLINK, TG_COUNT };
typedef struct {
  uint8_t kind, spot, tier, cost, gate, hinted, flags; /* flags: 1 recipe 2 reaction 4 collapse 8 timed */
  uint8_t extra_steps, hazards, enemies;
  uint16_t need_any;      /* at least one of these means opens it (0 = none) */
  uint8_t ok_before, ok;  /* reachable before / after the placement guarantee */
} mx_treasure;
extern const char *const mx_tk_name[TK_COUNT];
extern const char *const mx_sp_name[SP_COUNT];
extern const char *const mx_tg_name[TG_COUNT];
#define MAX_TREASURE 4

typedef struct {
  uint8_t type, biome, floor, trick, nmobs, ntiles, modifier, optional;
  mx_mob mob[MAX_STAGE_MOBS];
  uint8_t count[MAX_STAGE_MOBS];
  uint8_t tile[MAX_TILES];
  uint16_t need, have_before, have;  /* means needed / room had pre-guarantee / after */
  uint16_t pickup;                   /* means added by the guarantee step as pickups */
  uint8_t added_tiles, recipe, phases;
  uint16_t have_main;                /* means at the end of the main-path step (treasure may only add) */
  uint8_t ntreasure, tbudget;
  mx_treasure tr[MAX_TREASURE];
  uint8_t steps, budget, conds;
  uint16_t diff;
  uint8_t boss_ok;
  mx_boss boss;
} mx_stage;

typedef struct {
  int8_t stand[FC_COUNT];
  int8_t lean[LN_COUNT];
  uint8_t gen;
  uint8_t order[RUN_BIOMES];
  uint8_t nstages;
  mx_stage st[MAX_RUN_STAGES];
  /* stats */
  uint8_t guarantee_adds, gen_rerolls, trick_rerolls, trick_forced, trick_converted, boss_fallbacks;
} mx_run;

void mx_run_generate(mx_run *run, uint32_t seed, uint8_t gen, const int8_t *stand, const int8_t *lean);
uint8_t mx_gen_mob(mx_rng *r, const mx_run *run, uint8_t biome, uint8_t depth, uint8_t stype,
                   const uint32_t *hist, uint8_t nhist, mx_mob *out);
uint16_t mx_tiles_means(const uint8_t *tiles, uint8_t n);
uint8_t mx_gen_boss(mx_rng *r, uint8_t biome, uint8_t phases, uint8_t prev_core, mx_boss *b);
extern uint8_t mx_guarantee_on;
extern uint32_t mx_stat_mob_rejects, mx_stat_mob_tries;

#endif
