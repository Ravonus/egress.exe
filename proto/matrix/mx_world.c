/* WORLD matrix: biomes x stage types x modifiers, the stage deck, the fairness guarantee,
 * and the coupling to the CHAT/truth matrix (faction standing, story leaning). */
#include "mx.h"

const char *const mx_biome_name[BI_COUNT] = {
  "OFFICE", "VENTS", "FLOODED ATRIUM", "SERVER FARM", "FOOD COURT", "DREAM GARDEN", "ROOFTOP SKY", "GLITCH CORE"
};
const char *const mx_stage_name[ST_COUNT] = {
  "puzzle room", "holdout", "escort", "chase/collapse", "hunt", "mid-boss", "boss", "heist/trade",
  "dark room", "mix under pressure", "rest/landing", "glitch rift"
};
const char *const mx_stage_win[ST_COUNT] = {
  "reach the egress door by changing tiles with the room's trick reaction (solver-verified)",
  "survive N waves / T seconds on a shrinking floor",
  "walk the NPC to the door; NPC HP > 0",
  "reach the exit before the floor collapses behind you",
  "kill the marked target (conditional kill rule)",
  "break a 2-phase guardian",
  "break a 3-phase boss (each phase its own weak point)",
  "leave with the item without the alarm, or trade it",
  "light the exit: braziers (HOT) or reflect light (SHINY); invisible foes",
  "make the named recipe at the pedestal while waves attack",
  "none: heal, trader, chat (truth matrix)",
  "optional: close the rift (DISPEL/kill core) for a glitch mod"
};
const char *const mx_fc_name[FC_COUNT] = { "COMPILE", "CHARGE", "SKY", "WATCH", "ROOT", "LOCK" };
const char *const mx_ln_name[LN_COUNT] = { "WORK", "DREAM", "ESCAPE", "GLITCH" };
const char *const mx_md_name[MD_COUNT] = { "-", "flooded", "dark", "windy", "overgrown", "glitchy", "heat", "collapsing" };

const uint8_t mx_fc_tags[FC_COUNT][2] = {
  { MX_GLITCH, MX_MAGIC }, { MX_SHOCK, MX_HOT }, { MX_AIRY, MX_COLD },
  { MX_SHINY, MX_SHOCK }, { MX_ALIVE, MX_WET }, { MX_STONE, MX_COLD }
};
const uint8_t mx_biome_fc[BI_COUNT] = { FC_WATCH, FC_CHARGE, FC_ROOT, FC_COMPILE, FC_LOCK, FC_WATCH, FC_SKY, FC_COMPILE };
static const uint8_t biome_md[BI_COUNT] = { MD_DARK, MD_WINDY, MD_FLOODED, MD_HEAT, MD_HEAT, MD_OVERGROWN, MD_WINDY, MD_GLITCHY };

/* WALKER FLYER PHASER BURROW CRAWL GRAV HEAVY TELE SWARM TURRET MIMIC */
const uint8_t mx_biome_body_w[BI_COUNT][BD_COUNT] = {
  { 8, 3, 2, 1, 4, 4, 3, 2, 3, 3, 6 }, { 4, 6, 3, 2, 7, 3, 2, 2, 7, 3, 2 },
  { 5, 6, 2, 4, 3, 3, 2, 1, 4, 2, 3 }, { 4, 2, 5, 1, 4, 2, 4, 5, 4, 6, 3 },
  { 7, 3, 2, 2, 2, 4, 5, 2, 5, 3, 6 }, { 5, 6, 5, 3, 2, 2, 2, 5, 4, 2, 4 },
  { 4, 9, 2, 1, 5, 6, 2, 3, 4, 3, 1 }, { 3, 3, 7, 2, 3, 2, 2, 8, 4, 4, 5 },
};
/* LUNGE PROJ SLAM BEAM TRAIL SUMMON GRAB TERRA */
const uint8_t mx_biome_atk_w[BI_COUNT][AT_COUNT] = {
  { 6, 5, 3, 2, 2, 3, 4, 2 }, { 5, 4, 3, 2, 5, 3, 3, 3 }, { 4, 4, 3, 2, 4, 4, 3, 6 },
  { 3, 6, 2, 6, 3, 3, 2, 3 }, { 6, 4, 5, 2, 3, 4, 4, 2 }, { 3, 5, 3, 3, 3, 6, 3, 4 },
  { 5, 6, 3, 4, 2, 2, 4, 3 }, { 3, 4, 3, 5, 4, 5, 3, 6 },
};
/* PLAIN ARMOR WETSHOCK CRUSH SPLITS REFORM REFLECT DARK */
const uint8_t mx_biome_def_w[BI_COUNT][DF_COUNT] = {
  { 8, 4, 2, 4, 2, 1, 3, 2 }, { 6, 3, 3, 3, 4, 2, 3, 5 }, { 6, 3, 5, 2, 3, 4, 3, 2 },
  { 6, 4, 5, 3, 3, 2, 4, 3 }, { 7, 4, 3, 4, 4, 2, 3, 2 }, { 5, 2, 2, 2, 4, 5, 4, 4 },
  { 6, 4, 3, 4, 3, 3, 4, 2 }, { 4, 3, 3, 3, 4, 5, 5, 5 },
};
/* HOT COLD WET SHOCK SHINY ALIVE MAGIC GLITCH STONE AIRY none */
const uint8_t mx_biome_el_w[BI_COUNT][EL_COUNT] = {
  { 2, 2, 2, 4, 4, 1, 1, 1, 3, 1, 8 }, { 5, 2, 2, 2, 1, 2, 1, 1, 3, 7, 4 },
  { 1, 4, 8, 2, 2, 6, 2, 1, 2, 1, 3 }, { 5, 3, 1, 8, 3, 1, 2, 4, 2, 1, 3 },
  { 6, 3, 5, 1, 3, 2, 1, 1, 2, 1, 5 }, { 1, 2, 3, 1, 4, 7, 7, 2, 1, 3, 2 },
  { 1, 6, 3, 6, 2, 1, 1, 1, 2, 8, 2 }, { 2, 2, 2, 3, 2, 1, 5, 10, 2, 2, 2 },
};
/* COWARD AGGRO FLANK GUARD LEADER THIEF */
const uint8_t mx_biome_beh_w[BI_COUNT][BH_COUNT] = {
  { 4, 5, 3, 4, 3, 3 }, { 3, 5, 6, 2, 3, 3 }, { 4, 4, 3, 5, 3, 2 }, { 3, 4, 4, 6, 3, 2 },
  { 3, 5, 3, 3, 3, 6 }, { 5, 3, 4, 3, 4, 3 }, { 4, 5, 5, 2, 3, 3 }, { 3, 6, 4, 3, 4, 3 },
};
/* PUZZLE HOLDOUT ESCORT CHASE HUNT MIDBOSS BOSS HEIST DARK MIX REST RIFT (boss/mid/rest/rift are placed, not drawn) */
const uint8_t mx_biome_stage_w[BI_COUNT][ST_COUNT] = {
  { 8, 3, 4, 2, 3, 0, 0, 3, 3, 3, 0, 0 }, { 5, 3, 2, 6, 4, 0, 0, 2, 6, 2, 0, 0 },
  { 7, 3, 4, 3, 3, 0, 0, 2, 2, 5, 0, 0 }, { 5, 6, 3, 3, 4, 0, 0, 4, 4, 3, 0, 0 },
  { 4, 4, 5, 3, 3, 0, 0, 7, 2, 5, 0, 0 }, { 6, 2, 3, 3, 5, 0, 0, 2, 5, 4, 0, 0 },
  { 4, 4, 2, 7, 4, 0, 0, 2, 2, 3, 0, 0 }, { 4, 5, 2, 5, 5, 0, 0, 2, 4, 4, 0, 0 },
};
/* FLOOR WATER ICE PIT WALL CRACK VINE MUD LAVA MIRROR VENT NODE CRATE LAMP RIFT STEAM BRIDGE SNOW LIT TURRET */
const uint8_t mx_biome_tile_w[BI_COUNT][TL_COUNT] = {
  { 0, 2, 0, 3, 4, 3, 1, 0, 0, 3, 2, 4, 6, 5, 0, 0, 0, 0, 0, 2 },
  { 0, 1, 1, 5, 4, 3, 0, 1, 2, 1, 8, 3, 4, 2, 0, 0, 0, 0, 0, 2 },
  { 0, 8, 2, 3, 2, 2, 6, 4, 0, 2, 1, 2, 3, 2, 0, 0, 0, 0, 0, 1 },
  { 0, 1, 2, 2, 4, 3, 0, 0, 2, 3, 4, 8, 4, 3, 1, 0, 0, 0, 0, 3 },
  { 0, 4, 2, 2, 3, 2, 2, 1, 3, 4, 2, 2, 6, 4, 0, 0, 0, 0, 0, 2 },
  { 0, 4, 1, 3, 2, 2, 7, 2, 0, 5, 2, 1, 3, 3, 2, 0, 0, 0, 0, 1 },
  { 0, 2, 4, 6, 2, 3, 1, 0, 0, 2, 6, 4, 4, 2, 0, 0, 0, 0, 0, 2 },
  { 0, 2, 2, 4, 3, 3, 2, 1, 2, 3, 2, 4, 4, 2, 8, 0, 0, 0, 0, 3 },
};

/* CRUCIBLE recipes reused as MIX objectives: means are the base tags you must bring to the pedestal */
#define H TB(MX_HOT)
#define W TB(MX_WET)
#define S TB(MX_STONE)
#define A TB(MX_AIRY)
const mx_recipe mx_recipes[MX_RECIPES] = {
  { "STEAM", H | W, 1 }, { "LAVA", S | H, 1 }, { "SNOW", A | W, 1 }, { "PLANT", S | W, 1 },
  { "METAL", H | S, 1 }, { "GLASS", H | S, 1 }, { "ENERGY", A | H, 1 }, { "ICE", W | A, 2 },
  { "TREE", S | W, 2 }, { "BRICK", H | W | S, 2 }, { "SWORD", H | S, 2 }, { "SHOVEL", H | S, 2 },
  { "LANTERN", H | S, 2 }, { "MIRROR", H | S, 3 }, { "ELECTRIC", A | H | S, 3 }, { "LASER", A | H | S | TB(MX_SHINY), 4 },
};
#undef H
#undef W
#undef S
#undef A

uint32_t mx_stat_mob_rejects, mx_stat_mob_tries;
uint8_t mx_guarantee_on = 1;

static int8_t clamp8(int16_t v) { return (int8_t)(v > 100 ? 100 : (v < -100 ? -100 : v)); }
static uint8_t clampw(int16_t v) { return (uint8_t)(v < 1 ? 1 : (v > 60 ? 60 : v)); }

uint16_t mx_tiles_means(const uint8_t *tiles, uint8_t n)
{
  uint16_t m = 0;
  uint8_t i, t;
  for (i = 0; i < n; i++) {
    t = mx_tile_tag[tiles[i]];
    if (t < MX_TAGS) m |= TB(t);
    if (tiles[i] == TL_CRATE || tiles[i] == TL_PIT) m |= M_CRUSH;
    if (tiles[i] == TL_TURRET) m |= M_PROJ;
    if (tiles[i] == TL_LAMP || tiles[i] == TL_LIGHT) m |= M_LIGHT;
  }
  if (m & (TB(MX_HOT) | TB(MX_SHINY))) m |= M_LIGHT;
  return m;
}

/* ---------- mob generation (biome weights + coupling + run history) ---------- */
uint8_t mx_gen_mob(mx_rng *r, const mx_run *run, uint8_t biome, uint8_t depth, uint8_t stype,
                   const uint32_t *hist, uint8_t nhist, mx_mob *out)
{
  uint8_t wb[BD_COUNT], wa[AT_COUNT], wd[DF_COUNT], we[EL_COUNT], wh[BH_COUNT];
  uint8_t i, f, tries, soft;
  int16_t v;
  for (i = 0; i < BD_COUNT; i++) wb[i] = mx_biome_body_w[biome][i];
  for (i = 0; i < AT_COUNT; i++) wa[i] = mx_biome_atk_w[biome][i];
  for (i = 0; i < DF_COUNT; i++) wd[i] = mx_biome_def_w[biome][i];
  for (i = 0; i < EL_COUNT; i++) we[i] = mx_biome_el_w[biome][i];
  for (i = 0; i < BH_COUNT; i++) wh[i] = mx_biome_beh_w[biome][i];
  /* FUNCTION <- CHAT coupling: hated factions field their tags, liked ones stand down */
  for (f = 0; f < FC_COUNT; f++) {
    for (i = 0; i < 2; i++) {
      v = (int16_t)we[mx_fc_tags[f][i]];
      if (run->stand[f] < 0) v = (int16_t)(v + (-run->stand[f]) / 20);
      else v = (int16_t)(v - run->stand[f] / 40);
      we[mx_fc_tags[f][i]] = clampw(v);
    }
  }
  if (run->lean[LN_DREAM] > 0) { we[MX_MAGIC] = clampw((int16_t)(we[MX_MAGIC] + run->lean[LN_DREAM] / 32)); we[MX_ALIVE] = clampw((int16_t)(we[MX_ALIVE] + run->lean[LN_DREAM] / 32)); }
  if (run->lean[LN_GLITCH] > 0) we[MX_GLITCH] = clampw((int16_t)(we[MX_GLITCH] + run->lean[LN_GLITCH] / 25));
  /* depth opens up the nastier kill conditions */
  if (depth < 2) { wd[DF_REFORM] = (uint8_t)(wd[DF_REFORM] >> 1); wd[DF_WETSHOCK] = (uint8_t)(wd[DF_WETSHOCK] >> 1); wd[DF_PLAIN] = (uint8_t)(wd[DF_PLAIN] * 2); }
  /* stage-type bias */
  if (stype == ST_DARK) wd[DF_DARK] = (uint8_t)(wd[DF_DARK] * 4);
  if (stype == ST_HEIST) wh[BH_GUARD] = (uint8_t)(wh[BH_GUARD] * 3);
  if (stype == ST_ESCORT) { wh[BH_THIEF] = (uint8_t)(wh[BH_THIEF] * 2); wh[BH_FLANK] = (uint8_t)(wh[BH_FLANK] * 2); }
  if (stype == ST_RIFT) { we[MX_GLITCH] = (uint8_t)(we[MX_GLITCH] * 4); wb[BD_TELEPORT] = (uint8_t)(wb[BD_TELEPORT] * 3); wb[BD_PHASER] = (uint8_t)(wb[BD_PHASER] * 3); }
  if (stype == ST_HUNT) wd[DF_PLAIN] = 0;
  if (stype == ST_CHASE) { wb[BD_TURRET] = 0; wb[BD_MIMIC] = 0; }

  for (soft = 0; soft < 4; soft++) {
    for (tries = 0; tries < 64; tries++) {
      mx_stat_mob_tries++;
      out->body = mx_pick(r, wb, BD_COUNT);
      out->atk = mx_pick(r, wa, AT_COUNT);
      out->def = mx_pick(r, wd, DF_COUNT);
      out->el = mx_pick(r, we, EL_COUNT);
      out->beh = mx_pick(r, wh, BH_COUNT);
      out->af[0] = out->af[1] = AF_NONE;
      if (mx_mob_valid(out)) break;
      mx_stat_mob_rejects++;
    }
    if (tries == 64) { /* fallback that always passes */
      out->body = BD_WALKER; out->atk = AT_LUNGE; out->def = stype == ST_HUNT ? DF_ARMOR : DF_PLAIN;
      out->el = MX_TAGS; out->beh = BH_AGGRO;
    }
    /* run history: reroll a build already met this run (soft, 3 tries) */
    for (i = (uint8_t)(nhist > 24 ? nhist - 24 : 0); i < nhist; i++) if (hist[i] == mx_mob_id(out)) break; /* window: last 24 picks */
    if (i == nhist) return 1;
  }
  return 0;
}

static void gen_affixes(mx_rng *r, mx_mob *m, uint8_t n)
{
  uint8_t k, a, tries;
  for (k = 0; k < n; k++) {
    for (tries = 0; tries < 24; tries++) {
      a = mx_below(r, AF_COUNT);
      m->af[k] = a;
      if (mx_elite_valid(m)) break;
      m->af[k] = AF_NONE;
    }
  }
}

uint8_t mx_gen_boss(mx_rng *r, uint8_t biome, uint8_t phases, uint8_t prev_core, mx_boss *b)
{
  uint16_t tries;
  uint8_t i, wd[DF_COUNT];
  for (i = 0; i < DF_COUNT; i++) wd[i] = (uint8_t)(mx_biome_def_w[biome][i] + 2);
  for (tries = 0; tries < 2000; tries++) {
    b->core = mx_below(r, BC_COUNT);
    if (b->core == prev_core && tries < 1000) continue;
    for (i = 0; i < 3; i++) { b->atk[i] = mx_pick(r, mx_biome_atk_w[biome], AT_COUNT); b->weak[i] = mx_pick(r, wd, DF_COUNT); }
    b->hazard = mx_below(r, HZ_COUNT);
    if (biome == BI_ATRIUM && mx_below(r, 2)) b->hazard = HZ_FLOOD;
    if (biome == BI_GLITCH && mx_below(r, 3) == 0) b->hazard = HZ_DARK;
    b->el[0] = mx_pick(r, mx_biome_el_w[biome], MX_TAGS);
    b->el[1] = mx_pick(r, mx_biome_el_w[biome], MX_TAGS);
    if (phases == 2) { b->atk[2] = AT_COUNT; b->weak[2] = DF_COUNT; }
    if (phases == 2) {
      /* validate a 2-phase guardian by checking it as a 3-phase with a harmless dummy */
      mx_boss t = *b;
      uint8_t d;
      for (d = 0; d < AT_COUNT; d++) if (d != t.atk[0] && d != t.atk[1]) break;
      t.atk[2] = d;
      for (d = 0; d < DF_COUNT; d++)
        if (d != t.weak[0] && d != t.weak[1] && (d == DF_ARMOR || d == DF_CRUSH || d == DF_REFORM)) break;
      t.weak[2] = d < DF_COUNT ? d : DF_ARMOR;
      if (mx_boss_valid(&t)) return 1;
      { /* the dummy may be the problem: accept if any dummy works */
        uint8_t da, dd;
        for (da = 0; da < AT_COUNT; da++) for (dd = 1; dd < DF_COUNT; dd++) {
          if (da == t.atk[0] || da == t.atk[1] || dd == t.weak[0] || dd == t.weak[1]) continue;
          t.atk[2] = da; t.weak[2] = dd;
          if (mx_boss_valid(&t)) return 1;
        }
      }
    } else if (mx_boss_valid(b)) return 1;
  }
  { /* deterministic fallback: walk the template space from a seeded start until something is valid */
    uint32_t start = mx_rand(r), k;
    for (k = 0; k < 400000u; k++) {
      uint32_t x = (start + k * 7919u) % 3145728u; /* 4*8*8*8*8*8*8*6 = 3145728 */
      b->core = (uint8_t)(x & 3); x >>= 2;
      b->atk[0] = (uint8_t)(x & 7); x >>= 3; b->atk[1] = (uint8_t)(x & 7); x >>= 3; b->atk[2] = (uint8_t)(x & 7); x >>= 3;
      b->weak[0] = (uint8_t)(x & 7); x >>= 3; b->weak[1] = (uint8_t)(x & 7); x >>= 3; b->weak[2] = (uint8_t)(x & 7); x >>= 3;
      b->hazard = (uint8_t)(x % HZ_COUNT);
      b->el[0] = mx_pick(r, mx_biome_el_w[biome], MX_TAGS); b->el[1] = (uint8_t)((b->el[0] + 1 + (k % 9)) % MX_TAGS);
      if (mx_boss_valid(b)) { if (phases == 2) { b->atk[2] = AT_COUNT; b->weak[2] = DF_COUNT; } return 2; }
    }
  }
  return 0;
}

/* ---------- stage deck ---------- */
static const uint8_t stage_budget16[ST_COUNT] = { 4, 24, 12, 8, 14, 20, 28, 10, 12, 14, 0, 18 };
static const uint8_t stage_ntypes[ST_COUNT]   = { 1, 3, 2, 2, 2, 1, 1, 2, 2, 2, 0, 2 };
static const uint8_t src_tile[MX_TAGS] = { TL_LAMP, TL_ICE, TL_WATER, TL_NODE, TL_MIRROR, TL_VINE, TL_RIFT, TL_RIFT, TL_CRACK, TL_VENT };

static uint8_t trick_seen(const uint8_t *ring, uint8_t t)
{
  uint8_t i;
  if (t >= 200) return 0;
  for (i = 0; i < 6; i++) if (ring[i] == t) return 1;
  return 0;
}

static void drift(mx_run *run, mx_rng *r, const mx_stage *s)
{
  uint8_t i;
  int16_t d;
  switch (s->type) {
  case ST_HEIST: run->stand[FC_WATCH] = clamp8((int16_t)(run->stand[FC_WATCH] - 12)); run->stand[FC_LOCK] = clamp8((int16_t)(run->stand[FC_LOCK] - 6)); run->lean[LN_WORK] = clamp8((int16_t)(run->lean[LN_WORK] + 3)); break;
  case ST_ESCORT: run->stand[FC_ROOT] = clamp8((int16_t)(run->stand[FC_ROOT] + 8)); break;
  case ST_DARK: run->lean[LN_DREAM] = clamp8((int16_t)(run->lean[LN_DREAM] + 3)); break;
  case ST_CHASE: run->lean[LN_ESCAPE] = clamp8((int16_t)(run->lean[LN_ESCAPE] + 4)); break;
  case ST_RIFT: run->lean[LN_GLITCH] = clamp8((int16_t)(run->lean[LN_GLITCH] + 10)); run->stand[FC_COMPILE] = clamp8((int16_t)(run->stand[FC_COMPILE] - 8)); break;
  case ST_PUZZLE: if (s->biome == BI_OFFICE || s->biome == BI_SERVER || s->biome == BI_MALL) run->lean[LN_WORK] = clamp8((int16_t)(run->lean[LN_WORK] + 3)); break;
  case ST_REST: /* stand-in for the player's chat choices: one faction and one leaning move */
    i = mx_below(r, FC_COUNT); d = (int16_t)((int16_t)mx_below(r, 25) - 10);
    run->stand[i] = clamp8((int16_t)(run->stand[i] + d));
    i = mx_below(r, LN_COUNT); run->lean[i] = clamp8((int16_t)(run->lean[i] + 8));
    break;
  default: break;
  }
  for (i = 0; i < s->nmobs; i++) /* killing a faction's kind costs a little standing */
    if (s->mob[i].el < MX_TAGS) {
      uint8_t f;
      for (f = 0; f < FC_COUNT; f++)
        if (mx_fc_tags[f][0] == s->mob[i].el || mx_fc_tags[f][1] == s->mob[i].el)
          run->stand[f] = clamp8((int16_t)(run->stand[f] - 1));
    }
}

static uint8_t pick_middle_biome(mx_run *run, mx_rng *r, uint8_t used)
{
  /* CHAT -> WORLD coupling: leaning tilts which mood comes next */
  static const uint8_t mid[6] = { BI_VENTS, BI_ATRIUM, BI_SERVER, BI_MALL, BI_DREAM, BI_ROOF };
  uint8_t w[6], i;
  int16_t v;
  for (i = 0; i < 6; i++) {
    v = 6;
    if (mid[i] == BI_SERVER || mid[i] == BI_MALL) v = (int16_t)(v + run->lean[LN_WORK] / 12);
    if (mid[i] == BI_DREAM || mid[i] == BI_ATRIUM) v = (int16_t)(v + run->lean[LN_DREAM] / 12);
    if (mid[i] == BI_ROOF || mid[i] == BI_VENTS) v = (int16_t)(v + run->lean[LN_ESCAPE] / 12);
    if (run->stand[mx_biome_fc[mid[i]]] < -40) v = (int16_t)(v + 2); /* grudges pull you into their turf */
    w[i] = (used & (1u << i)) ? 0 : clampw(v);
  }
  i = mx_pick(r, w, 6);
  return (uint8_t)(i | (mid[i] << 4));
}

static void place_treasure(mx_run *run, mx_rng *r, mx_stage *s);
static void gen_stage(mx_run *run, mx_rng *r, mx_stage *s, uint8_t type, uint8_t biome, uint8_t floor,
                      uint8_t *ring, uint32_t *hist, uint8_t *nhist, uint8_t *prev_core, uint8_t prev_type)
{
  uint8_t i, k, n, depth = floor, budget, share, el_pct, tries;
  uint16_t self = 0, need = 0;
  s->type = type; s->biome = biome; s->floor = floor; s->optional = (uint8_t)(type == ST_RIFT);
  s->nmobs = 0; s->ntiles = 0; s->pickup = 0; s->added_tiles = 0; s->recipe = 255; s->phases = 0; s->boss_ok = 1;
  /* tiles: 3-5 features from the biome, plus help from liked factions */
  n = (uint8_t)(3 + mx_below(r, 3));
  for (i = 0; i < n; i++) s->tile[s->ntiles++] = mx_pick(r, mx_biome_tile_w[biome], TL_COUNT);
  for (i = 0; i < FC_COUNT && s->ntiles < MAX_TILES - 3; i++)
    if (run->stand[i] >= 40 && mx_below(r, 2)) s->tile[s->ntiles++] = src_tile[mx_fc_tags[i][mx_below(r, 2)]];
  /* modifier */
  s->modifier = MD_NONE;
  if (type != ST_REST && mx_below(r, 100) < 15 + depth * 3) {
    s->modifier = biome_md[biome];
    if (run->lean[LN_ESCAPE] > 40 && mx_below(r, 3) == 0) s->modifier = MD_COLLAPSE;
    if (run->lean[LN_GLITCH] > 40 && mx_below(r, 3) == 0) s->modifier = MD_GLITCHY;
  }
  if (type == ST_DARK) s->modifier = MD_DARK;
  if (s->modifier == MD_FLOODED && s->ntiles < MAX_TILES) s->tile[s->ntiles++] = TL_WATER;
  if (s->modifier == MD_WINDY && s->ntiles < MAX_TILES) s->tile[s->ntiles++] = TL_VENT;
  if (s->modifier == MD_OVERGROWN && s->ntiles < MAX_TILES) s->tile[s->ntiles++] = TL_VINE;
  if (s->modifier == MD_GLITCHY && s->ntiles < MAX_TILES) s->tile[s->ntiles++] = TL_RIFT;
  if (s->modifier == MD_HEAT && s->ntiles < MAX_TILES) s->tile[s->ntiles++] = TL_LAVA;

  /* trick (seen-history keeps a trick from repeating within 6 stages) */
  s->trick = (uint8_t)(200 + type);
  {
    uint8_t last_t = s->trick;
    uint16_t last_need = 0;
    static const uint8_t dark_rx[5] = { 10 /* LANTERN */, RX_REFLECT, 25 /* LASER */, 18 /* SNOW */, 20 /* MIRAGE */ };
    static const uint16_t dark_need[5] = {
      TB(MX_HOT), TB(MX_SHINY), TB(MX_SHOCK) | TB(MX_SHINY), TB(MX_COLD) | TB(MX_AIRY), TB(MX_WET) | TB(MX_SHINY) };
    for (tries = 0; tries < 48; tries++) {
      uint8_t t = s->trick;
      if ((type == ST_PUZZLE || type == ST_RIFT) && tries && (tries % 16) == 0 && s->ntiles < MAX_TILES - 3)
        s->tile[s->ntiles++] = mx_pick(r, mx_biome_tile_w[biome], TL_COUNT); /* every pair was recent: add a feature */
      uint16_t nd = 0;
      if (type == ST_PUZZLE || type == ST_RIFT) {
        uint8_t tile = s->tile[mx_below(r, s->ntiles)], tag = mx_below(r, MX_TAGS), rx, out;
        out = mx_tile_react(tile, tag, mx_rand(r), &rx);
        if (out == TL_KEEP || rx == RX_NONE) continue;
        t = rx; nd = TB(tag);
      } else if (type == ST_DARK) {
        uint8_t d = mx_below(r, 5);
        t = dark_rx[d]; nd = (uint16_t)(dark_need[d] | M_LIGHT);
      } else if (type == ST_MIX) {
        uint8_t maxs = (uint8_t)(1 + depth / 3), rc;
        rc = mx_below(r, MX_RECIPES);
        if (mx_recipes[rc].steps > maxs) continue;
        s->recipe = rc; t = (uint8_t)(100 + rc); nd = mx_recipes[rc].need;
      } else break;
      last_t = t; last_need = nd;
      if (!trick_seen(ring, t)) { s->trick = t; need = nd; break; }
      run->trick_rerolls++;
    }
    if (s->trick >= 200 && last_t < 200) {
      if ((type == ST_DARK || type == ST_MIX) && prev_type == ST_PUZZLE) {
        type = ST_HUNT; s->type = ST_HUNT; s->recipe = 255; run->trick_converted++; s->trick = (uint8_t)(200 + ST_HUNT);
      } else if (type == ST_DARK || type == ST_MIX) {
        /* every DARK/MIX/RIFT method was recent: this room becomes a puzzle room instead (many tricks) */
        type = ST_PUZZLE; s->type = ST_PUZZLE; s->recipe = 255; run->trick_converted++;
        for (tries = 0; tries < 32; tries++) {
          uint8_t tile = s->tile[mx_below(r, s->ntiles)], tag = mx_below(r, MX_TAGS), rx, out;
          out = mx_tile_react(tile, tag, mx_rand(r), &rx);
          if (out == TL_KEEP || rx == RX_NONE || trick_seen(ring, rx)) continue;
          s->trick = rx; need = TB(tag); break;
        }
      }
      if (s->trick >= 200 && type != ST_HUNT) { s->trick = last_t; need = last_need; run->trick_forced++; }
    }
  }
  if (s->trick < 200) { for (i = 5; i; i--) ring[i] = ring[i - 1]; ring[0] = s->trick; }
  if (type == ST_PUZZLE && s->trick >= 200) { /* nothing reacted: use the always-there crate push */
    s->tile[s->ntiles < MAX_TILES ? s->ntiles++ : 0] = TL_CRATE; need = M_CRUSH;
  }

  /* enemies */
  budget = (uint8_t)(8 + depth * 3 + run->gen * 2);
  budget = (uint8_t)((budget * stage_budget16[type]) >> 4);
  el_pct = (uint8_t)(4 + depth * 3 + run->gen); if (el_pct > 45) el_pct = 45;
  if (type == ST_BOSS || type == ST_MIDBOSS) {
    s->phases = (uint8_t)(type == ST_BOSS ? 3 : 2);
    s->boss_ok = mx_gen_boss(r, biome, s->phases, *prev_core, &s->boss);
    if (s->boss_ok == 2) run->boss_fallbacks++;
    *prev_core = s->boss.core;
    need |= mx_boss_means(&s->boss);
  }
  n = stage_ntypes[type];
  share = n ? (uint8_t)(budget / n) : 0;
  for (k = 0; k < n; k++) {
    mx_mob *m = &s->mob[s->nmobs];
    mx_gen_mob(r, run, biome, depth, (uint8_t)(k == 0 ? type : ST_HOLDOUT), hist, *nhist, m);
    if (type == ST_DARK && k == 0 && m->def != DF_DARK) { /* the dark room's resident */
      for (tries = 0; tries < 32 && m->def != DF_DARK; tries++) mx_gen_mob(r, run, biome, depth, type, hist, *nhist, m);
    }
    if ((type == ST_MIDBOSS || type == ST_BOSS) && k == 0) { /* adds for the arena: plain-ish */
      m->def = DF_PLAIN; if (!mx_mob_valid(m)) { m->body = BD_WALKER; m->atk = AT_LUNGE; m->beh = BH_AGGRO; m->el = MX_TAGS; }
    }
    if (mx_below(r, 100) < (type == ST_HUNT && k == 0 ? el_pct * 2 : el_pct))
      gen_affixes(r, m, (uint8_t)(1 + (depth >= 5 && mx_below(r, 3) == 0)));
    s->count[s->nmobs] = (uint8_t)(share / mx_mob_cost(m));
    if (s->count[s->nmobs] < 1) s->count[s->nmobs] = 1;
    if (s->count[s->nmobs] > 6) s->count[s->nmobs] = 6;
    if (m->body == BD_SWARM) s->count[s->nmobs] = 1; /* a swarm is one group */
    need |= mx_mob_means(m);
    self |= mx_mob_self_means(m);
    if (*nhist < 255) hist[(*nhist)++] = mx_mob_id(m);
    s->nmobs++;
  }
  if (s->modifier == MD_DARK) need |= M_LIGHT;
  if (type == ST_BOSS || type == ST_MIDBOSS) {
    for (i = 0; i < 3; i++) if (s->boss.atk[i] == AT_PROJ || s->boss.atk[i] == AT_BEAM) self |= M_PROJ;
    if (s->boss.hazard == HZ_DARK) need |= M_LIGHT;
  }
  s->need = need;
  s->have_before = (uint16_t)(mx_tiles_means(s->tile, s->ntiles) | self);
  s->have = s->have_before;
  /* guarantee: every needed means is placed in the room (tile if one fits, else a pickup) */
  if (mx_guarantee_on) {
    uint16_t miss = (uint16_t)(need & ~s->have);
    for (i = 0; i < 13; i++) {
      if (!(miss & (1u << i))) continue;
      if (i < MX_TAGS && s->ntiles < MAX_TILES && i != MX_MAGIC && i != MX_GLITCH) { s->tile[s->ntiles++] = src_tile[i]; s->added_tiles++; }
      else if ((1u << i) == M_CRUSH && s->ntiles < MAX_TILES) { s->tile[s->ntiles++] = TL_CRATE; s->added_tiles++; }
      else if ((1u << i) == M_PROJ && s->ntiles < MAX_TILES) { s->tile[s->ntiles++] = TL_TURRET; s->added_tiles++; }
      else if ((1u << i) == M_LIGHT && s->ntiles < MAX_TILES) { s->tile[s->ntiles++] = TL_LAMP; s->added_tiles++; }
      else s->pickup |= (uint16_t)(1u << i);
      run->guarantee_adds++;
    }
    s->have = (uint16_t)(mx_tiles_means(s->tile, s->ntiles) | self | s->pickup);
  }
  /* difficulty: solver steps estimate + enemy budget + condition count */
  switch (type) {
  case ST_PUZZLE: s->steps = (uint8_t)(3 + depth / 2 + mx_below(r, 3)); break;
  case ST_DARK: s->steps = (uint8_t)(2 + depth / 3 + mx_below(r, 2)); break;
  case ST_MIX: s->steps = (uint8_t)(s->recipe < MX_RECIPES ? mx_recipes[s->recipe].steps + 1 : 2); break;
  case ST_CHASE: s->steps = (uint8_t)(1 + depth / 4); break;
  case ST_HEIST: s->steps = (uint8_t)(2 + mx_below(r, 2)); break;
  case ST_RIFT: s->steps = (uint8_t)(2 + mx_below(r, 3)); break;
  case ST_MIDBOSS: s->steps = 2; break;
  case ST_BOSS: s->steps = 3; break;
  case ST_ESCORT: case ST_HUNT: s->steps = 1; break;
  default: s->steps = 0; break;
  }
  {
    uint8_t used = 0, defs = 0, c = 1;
    for (k = 0; k < s->nmobs; k++) {
      used = (uint8_t)(used + mx_mob_cost(&s->mob[k]) * s->count[k]);
      if (s->mob[k].def != DF_PLAIN && !(defs & (1u << s->mob[k].def))) { defs |= (uint8_t)(1u << s->mob[k].def); c++; }
      if (s->mob[k].af[0] != AF_NONE) c++;
      if (s->mob[k].af[1] != AF_NONE) c++;
    }
    if (s->phases) { used = (uint8_t)(used + budget); c = (uint8_t)(c + s->phases); }
    if (s->modifier != MD_NONE) c++;
    s->budget = used; s->conds = type == ST_REST ? 0 : c;
    s->diff = (uint16_t)(s->steps * 3 + used + c * 4);
    if (type == ST_REST) s->diff = 0;
  }
  s->have_main = s->have;
  place_treasure(run, r, s);
}

/* ---------- treasure ---------- */
const char *const mx_tk_name[TK_COUNT] = { "tokens", "rare ingredient", "matrix piece", "lore fragment", "lineage relic", "cosmetic avatar part", "cursed item" };
const char *const mx_sp_name[SP_COUNT] = {
  "raised block", "ledge over void", "crumble floor", "behind a hazard", "flyer route", "assembling/collapsing card",
  "under foam/rubble/CRT", "in drywall", "crumbling patch", "under water (freeze/drain)"
};
const char *const mx_tg_name[TG_COUNT] = { "-", "SMALL (vent)", "HEAVY (two-block push)", "BLINK (gap)" };
typedef struct { uint8_t steps, haz, flags, gate; uint16_t need; } sp_rule;
static const sp_rule sp_rules[SP_COUNT] = {
  { 3, 0, 0, TG_HEAVY, M_CRUSH },
  { 4, 1, 2, TG_BLINK, TB(MX_ALIVE) | TB(MX_COLD) },          /* vine bridge or ice path */
  { 2, 1, 8, TG_NONE, 0 },
  { 3, 2, 0, TG_NONE, TB(MX_COLD) | TB(MX_WET) },             /* cool the lava / douse the steam */
  { 5, 2, 2, TG_BLINK, TB(MX_AIRY) | TB(MX_COLD) },           /* gust a crate across / freeze the water */
  { 3, 1, 4 | 8, TG_NONE, 0 },
  { 2, 0, 0, TG_HEAVY, M_CRUSH | TB(MX_STONE) },
  { 2, 0, 2, TG_SMALL, TB(MX_COLD) | TB(MX_ALIVE) | TB(MX_HOT) }, /* CRACK / ROOTBREAK / MELT */
  { 2, 1, 4, TG_NONE, 0 },
  { 3, 0, 2, TG_NONE, TB(MX_COLD) | TB(MX_HOT) },             /* FREEZE it or boil it off (STEAM) */
};
static const uint8_t tier_kind_w[4][TK_COUNT] = {
  { 6, 3, 0, 0, 0, 2, 0 }, { 3, 4, 0, 3, 0, 2, 1 }, { 0, 2, 4, 3, 0, 1, 2 }, { 0, 0, 3, 1, 4, 0, 2 }
};
static const uint8_t type_tbonus[ST_COUNT] = { 1, 0, 0, 0, 1, 2, 3, 2, 1, 1, 0, 3 };

static void place_treasure(mx_run *run, mx_rng *main_r, mx_stage *s)
{
  mx_rng r;           /* own stream: treasure never perturbs the main path's generation */
  uint8_t left, k, tries, enemies_max = 0, i;
  mx_seed(&r, main_r->s ^ 0x7EA5u);
  for (k = 0; k < s->nmobs; k++) enemies_max = (uint8_t)(enemies_max + s->count[k]);
  if (enemies_max > 4) enemies_max = 4;
  s->tbudget = (uint8_t)(s->type == ST_REST ? 1 : 2 + s->floor / 2 + type_tbonus[s->type]);
  left = s->tbudget; s->ntreasure = 0;
  while (left && s->ntreasure < MAX_TREASURE) {
    mx_treasure *t = &s->tr[s->ntreasure];
    for (tries = 0; tries < 4; tries++) {
      const sp_rule *sr;
      t->spot = mx_below(&r, SP_COUNT); sr = &sp_rules[t->spot];
      t->extra_steps = (uint8_t)(sr->steps + mx_below(&r, 3) + s->floor / 4);
      t->hazards = (uint8_t)(sr->haz + (s->modifier != MD_NONE && mx_below(&r, 2)));
      t->enemies = mx_below(&r, (uint8_t)(enemies_max + 1));
      t->flags = sr->flags;
      t->need_any = sr->need;
      t->hinted = (uint8_t)(t->spot >= SP_FIRST_HIDDEN && mx_below(&r, 10) < 3);
      t->gate = (uint8_t)((sr->gate != TG_NONE && mx_below(&r, 100) < 30) ? sr->gate : TG_NONE);
      if (t->gate) t->need_any = 0;                        /* the trait IS the way in */
      if (!t->gate && t->spot >= SP_FIRST_HIDDEN && mx_below(&r, 100) < 12) { t->flags |= 1; t->need_any = TB(MX_HOT); } /* make a LANTERN to see it */
      t->cost = (uint8_t)(t->extra_steps + t->hazards * 3 + t->enemies * 2 + ((t->flags & 1) ? 4 : 0) + ((t->flags & 2) ? 3 : 0) +
                          ((t->flags & 4) ? 3 : 0) + ((t->flags & 8) ? 3 : 0) + (t->gate ? 2 : 0) +
                          (t->spot >= SP_FIRST_HIDDEN ? (t->hinted ? 1 : 2) : 0));
      t->tier = (uint8_t)(t->cost <= 7 ? 0 : t->cost <= 11 ? 1 : t->cost <= 15 ? 2 : 3);
      if ((uint8_t)(t->tier + 1) <= left) break;
    }
    if (tries == 4) break;
    t->kind = mx_pick(&r, tier_kind_w[t->tier], TK_COUNT);
    t->ok_before = (uint8_t)(!t->need_any || (t->need_any & s->have));
    t->ok = t->ok_before;
    if (!t->ok && mx_guarantee_on) { /* place a source for the cheapest opener, off the main path */
      for (i = 0; i < 13; i++) if (t->need_any & (1u << i)) break;
      if (i < MX_TAGS && i != MX_MAGIC && s->ntiles < MAX_TILES) {
        static const uint8_t st[MX_TAGS] = { TL_LAMP, TL_ICE, TL_WATER, TL_NODE, TL_MIRROR, TL_VINE, TL_RIFT, TL_RIFT, TL_CRACK, TL_VENT };
        s->tile[s->ntiles++] = st[i];
      } else if ((1u << i) == M_CRUSH && s->ntiles < MAX_TILES) s->tile[s->ntiles++] = TL_CRATE;
      else s->pickup |= (uint16_t)(1u << i);
      s->have = (uint16_t)(s->have | mx_tiles_means(s->tile, s->ntiles) | s->pickup);
      t->ok = (uint8_t)((t->need_any & s->have) != 0);
      run->guarantee_adds++;
    }
    left = (uint8_t)(left - (t->tier + 1));
    s->ntreasure++;
  }
}

void mx_run_generate(mx_run *run, uint32_t seed, uint8_t gen, const int8_t *stand, const int8_t *lean)
{
  mx_rng r;
  uint8_t f, slot, i, type, prev = 255, used_mid = 0, ring[6], nhist = 0, prev_core = 255;
  uint8_t recent[3] = { 255, 255, 255 };
  uint8_t w[ST_COUNT];
  static uint32_t hist[256];
  mx_seed(&r, seed);
  for (i = 0; i < FC_COUNT; i++) run->stand[i] = stand[i];
  for (i = 0; i < LN_COUNT; i++) run->lean[i] = lean[i];
  for (i = 0; i < 6; i++) ring[i] = 255;
  run->gen = gen; run->nstages = 0; run->guarantee_adds = 0; run->gen_rerolls = 0; run->trick_rerolls = 0; run->trick_forced = 0; run->trick_converted = 0; run->boss_fallbacks = 0;
  run->order[0] = BI_OFFICE; run->order[RUN_BIOMES - 1] = BI_GLITCH;
  for (f = 0; f < RUN_FLOORS; f++) {
    uint8_t bi = (uint8_t)(f / FLOORS_PER_BIOME), biome;
    if (f % FLOORS_PER_BIOME == 0 && bi > 0 && bi < RUN_BIOMES - 1) {
      uint8_t p = pick_middle_biome(run, &r, used_mid);
      used_mid |= (uint8_t)(1u << (p & 15));
      run->order[bi] = (uint8_t)(p >> 4);
    }
    biome = run->order[bi];
    for (slot = 0; slot < STAGES_PER_FLOOR; slot++) {
      if (f == RUN_FLOORS - 1 && slot == STAGES_PER_FLOOR - 1) type = ST_BOSS;
      else if (f % FLOORS_PER_BIOME == 1 && slot == STAGES_PER_FLOOR - 1) type = ST_MIDBOSS;
      else {
        for (i = 0; i < ST_COUNT; i++) w[i] = mx_biome_stage_w[biome][i];
        if (f < 3) w[ST_PUZZLE] = (uint8_t)(w[ST_PUZZLE] + 3);
        w[ST_HOLDOUT] = (uint8_t)(w[ST_HOLDOUT] + f / 3);
        w[ST_HUNT] = (uint8_t)(w[ST_HUNT] + f / 4);
        if (f == 0) w[ST_HUNT] = 0;
        /* CHAT -> WORLD: standing reshapes the deck */
        if (run->stand[FC_WATCH] > 30) w[ST_HEIST] = (uint8_t)(w[ST_HEIST] + 3);
        if (run->stand[FC_LOCK] > 30) w[ST_PUZZLE] = (uint8_t)(w[ST_PUZZLE] + 3);
        if (run->stand[FC_CHARGE] < -30) w[ST_HOLDOUT] = (uint8_t)(w[ST_HOLDOUT] + 3);
        if (run->stand[FC_SKY] < -30) w[ST_CHASE] = (uint8_t)(w[ST_CHASE] + 3);
        if (run->stand[FC_ROOT] > 30) w[ST_ESCORT] = (uint8_t)(w[ST_ESCORT] + 2);
        if (run->stand[FC_COMPILE] < -30) w[ST_DARK] = (uint8_t)(w[ST_DARK] + 2);
        /* no back-to-back, and the last 3 types are damped (seen history) */
        if (prev < ST_COUNT) w[prev] = 0;
        for (i = 0; i < 3; i++) if (recent[i] < ST_COUNT && recent[i] != prev) w[recent[i]] = (uint8_t)((w[recent[i]] + 3) >> 2);
        type = mx_pick(&r, w, ST_COUNT);
        if (type == prev) { run->gen_rerolls++; type = ST_PUZZLE == prev ? ST_HUNT : ST_PUZZLE; }
      }
      gen_stage(run, &r, &run->st[run->nstages], type, biome, f, ring, hist, &nhist, &prev_core, prev);
      drift(run, &r, &run->st[run->nstages]);
      type = run->st[run->nstages].type; /* may have been converted (trick history) */
      run->nstages++;
      recent[2] = recent[1]; recent[1] = recent[0]; recent[0] = type; prev = type;
    }
    if (f < RUN_FLOORS - 1) { /* landing between floors */
      gen_stage(run, &r, &run->st[run->nstages], ST_REST, biome, f, ring, hist, &nhist, &prev_core, prev);
      drift(run, &r, &run->st[run->nstages]);
      run->nstages++; recent[2] = recent[1]; recent[1] = recent[0]; recent[0] = ST_REST; prev = ST_REST;
      /* optional rift branch off the landing */
      if (mx_below(&r, 100) < 8 + (run->lean[LN_GLITCH] > 0 ? run->lean[LN_GLITCH] / 5 : 0) + (run->stand[FC_COMPILE] < -30 ? 6 : 0)) {
        gen_stage(run, &r, &run->st[run->nstages], ST_RIFT, BI_GLITCH, f, ring, hist, &nhist, &prev_core, prev);
        drift(run, &r, &run->st[run->nstages]);
        type = run->st[run->nstages].type;
        run->nstages++; recent[2] = recent[1]; recent[1] = recent[0]; recent[0] = type; prev = type;
      }
    }
  }
}
