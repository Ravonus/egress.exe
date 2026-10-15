/* Mob composer: axes, compatibility rules, elites, bosses, kill-condition means, names. */
#include "mx.h"

const char *const mx_body_name[BD_COUNT] = {
  "walker", "flyer", "phaser", "burrower", "wall-crawler", "gravity-bound", "heavy", "teleporter",
  "swarm", "turret", "mimic"
};
const char *const mx_atk_name[AT_COUNT] = {
  "lunge", "projectile", "slam/ring", "beam", "trail hazard", "summon", "grab/pull", "terrain change"
};
const char *const mx_def_name[DF_COUNT] = {
  "plain", "armoured front", "shield: wet then shock", "crush only", "splits on death",
  "reforms unless frozen", "reflected shots only", "invisible in dark"
};
const char *const mx_def_rule[DF_COUNT] = {
  "-", "flank (base dodge) or pierce", "needs WET + SHOCK", "needs crate/boulder or pit (CRUSH)",
  "AoE helps; splits capped at 2 gens", "needs COLD", "needs an enemy projectile (PROJ) + parry",
  "needs LIGHT (lamp, HOT or SHINY)"
};
const char *const mx_beh_name[BH_COUNT] = { "coward/kiter", "aggressive", "flanker", "guard", "pack leader", "thief" };
const char *const mx_af_name[AF_AURA0] = { "shielded", "vampiric", "splitting", "hasted", "teleport-on-hit", "collapse trail" };
const char *const mx_af_rule[AF_AURA0] = {
  "shield = 3 hits; not on wet/shock-shield mobs",
  "heals 25% of damage dealt; not on reformers",
  "splits once on death; not swarm/splitter/reformer",
  "x0.75 tell; refused if tell would drop under 12 f",
  "blinks 3 cells on hit; not armoured/turret/mimic",
  "drops a crumble trail; ground movers only, not in chase stages",
};
/* tells: frames of wind-up at 60 Hz; minimum readable tell is 12 f */
const uint8_t mx_atk_tell[AT_COUNT] = { 18, 16, 28, 36, 20, 30, 18, 32 };
/* 1 = tell is self-lit (glow) or audible so it reads in the dark */
const uint8_t mx_atk_lit[AT_COUNT]  = { 1, 1, 1, 1, 0, 1, 1, 0 };
#define MIN_TELL 12

uint8_t mx_mob_valid(const mx_mob *m)
{
  uint8_t b = m->body, a = m->atk, d = m->def, e = m->el, h = m->beh;
  /* stationary bodies */
  if (b == BD_TURRET) {
    if (a == AT_LUNGE || a == AT_GRAB || a == AT_TRAIL) return 0;
    if (h != BH_GUARD && h != BH_AGGRO) return 0;
    if (d == DF_SPLITS) return 0;
  }
  if (b == BD_MIMIC) {
    if (h != BH_GUARD && h != BH_AGGRO && h != BH_THIEF) return 0;
    if (a == AT_TRAIL || a == AT_SUMMON || a == AT_BEAM) return 0;
    if (d == DF_DARK || d == DF_SPLITS) return 0;
  }
  if (b == BD_SWARM) {
    if (a == AT_SUMMON || a == AT_BEAM || a == AT_GRAB) return 0;  /* sprite budget, readability */
    if (d == DF_ARMOR || d == DF_CRUSH || d == DF_SPLITS || d == DF_REFLECT) return 0;
    if (h == BH_GUARD || h == BH_THIEF) return 0;
  }
  if (b == BD_HEAVY) {
    if (h == BH_COWARD || h == BH_THIEF || h == BH_FLANK) return 0;
    if (a == AT_PROJ || a == AT_BEAM) return 0;
    if (e == MX_AIRY) return 0;
  }
  if (b == BD_FLYER) {
    if (a == AT_TRAIL || a == AT_TERRA) return 0;
    if (d == DF_CRUSH) return 0;
    if (e == MX_STONE) return 0;
  }
  if (b == BD_PHASER) {
    if (d == DF_ARMOR || d == DF_CRUSH) return 0;
    if (a == AT_TERRA) return 0;
    if (e == MX_STONE) return 0;
  }
  if (b == BD_BURROWER) {
    if (a == AT_BEAM || a == AT_PROJ) return 0;
    if (d == DF_CRUSH || d == DF_DARK) return 0; /* already hidden: no double invisibility */
    if (e == MX_AIRY || e == MX_SHINY) return 0;
  }
  if (b == BD_CRAWLER && (a == AT_SLAM || d == DF_ARMOR)) return 0;
  if (b == BD_GRAVITY && (e == MX_AIRY || d == DF_REFLECT)) return 0;
  if (b == BD_TELEPORT) {
    if (h == BH_GUARD) return 0;
    if (d == DF_CRUSH || d == DF_ARMOR) return 0;
  }
  /* behaviours */
  if (h == BH_THIEF && (b == BD_TURRET || b == BD_HEAVY)) return 0;
  if (h == BH_LEADER && (b == BD_TURRET || b == BD_MIMIC || b == BD_SWARM)) return 0;
  if (h == BH_COWARD && (a == AT_LUNGE || a == AT_GRAB || a == AT_SLAM)) return 0; /* kiters need reach */
  /* defences */
  if (d == DF_WETSHOCK && (e == MX_WET || e == MX_SHOCK || e == MX_STONE)) return 0; /* would self-solve or ground */
  if (d == DF_REFORM && (e == MX_HOT || e == MX_GLITCH)) return 0;                  /* can't hold a freeze */
  if (d == DF_REFLECT && a != AT_PROJ && a != AT_BEAM) return 0;                    /* must supply the shots */
  if (d == DF_DARK) {
    if (e == MX_SHINY || e == MX_HOT) return 0;  /* it glows */
    if (!mx_atk_lit[a]) return 0;                /* every attack needs a tell you can read */
  }
  if (d == DF_CRUSH && e == MX_AIRY) return 0;
  /* element coherence */
  if (e == MX_GLITCH && !(b == BD_TELEPORT || b == BD_PHASER || b == BD_MIMIC || b == BD_SWARM || b == BD_TURRET))
    return 0;
  if (a == AT_TERRA && (e == MX_TAGS || e == MX_SHINY || e == MX_MAGIC)) return 0; /* terrain must have a tag that changes tiles */
  if (a == AT_TRAIL && e == MX_TAGS) return 0;
  if (mx_atk_tell[a] < MIN_TELL) return 0;
  return 1;
}

uint8_t mx_affix_ok(const mx_mob *m, uint8_t a)
{
  uint8_t t;
  switch (a) {
  case AF_SHIELD: return (uint8_t)(m->def != DF_WETSHOCK);
  case AF_VAMP: return (uint8_t)(m->def != DF_REFORM && m->body != BD_SWARM);
  case AF_SPLIT: return (uint8_t)(m->body != BD_SWARM && m->def != DF_SPLITS && m->def != DF_REFORM && m->body != BD_TURRET);
  case AF_HASTE: return (uint8_t)(((mx_atk_tell[m->atk] * 3) >> 2) >= MIN_TELL && m->body != BD_TURRET);
  case AF_TPHIT: return (uint8_t)(m->def != DF_ARMOR && m->body != BD_TURRET && m->body != BD_MIMIC && m->body != BD_TELEPORT);
  case AF_COLLAPSE:
    return (uint8_t)(m->body == BD_WALKER || m->body == BD_HEAVY || m->body == BD_CRAWLER || m->body == BD_GRAVITY);
  default:
    t = (uint8_t)(a - AF_AURA0);
    if (t >= MX_TAGS || t == m->el) return 0;
    if (m->def == DF_WETSHOCK && (t == MX_WET || t == MX_SHOCK || t == MX_STONE)) return 0;
    if (m->def == DF_REFORM && (t == MX_HOT || t == MX_GLITCH)) return 0;
    if (m->def == DF_DARK && (t == MX_SHINY || t == MX_HOT)) return 0;
    if (m->el < MX_TAGS && mx_rx_table[m->el][t] == 28 /* GROUND */) return 0; /* self-cancelling */
    return 1;
  }
}

uint8_t mx_elite_valid(const mx_mob *m)
{
  uint8_t i, n = 0;
  for (i = 0; i < 2; i++) {
    if (m->af[i] == AF_NONE) continue;
    if (!mx_affix_ok(m, m->af[i])) return 0;
    n++;
  }
  if (n == 2) {
    if (m->af[0] == m->af[1]) return 0;
    if (m->af[0] >= AF_AURA0 && m->af[1] >= AF_AURA0) return 0;       /* one aura max */
    if ((m->af[0] == AF_VAMP && m->af[1] == AF_SHIELD) || (m->af[1] == AF_VAMP && m->af[0] == AF_SHIELD)) return 0;
    if ((m->af[0] == AF_HASTE && m->af[1] == AF_TPHIT) || (m->af[1] == AF_HASTE && m->af[0] == AF_TPHIT)) return 0;
    if ((m->af[0] == AF_SPLIT && m->af[1] == AF_VAMP) || (m->af[1] == AF_SPLIT && m->af[0] == AF_VAMP)) return 0;
  }
  return 1;
}

uint16_t mx_mob_means(const mx_mob *m)
{
  switch (m->def) {
  case DF_WETSHOCK: return (uint16_t)(TB(MX_WET) | TB(MX_SHOCK));
  case DF_CRUSH: return M_CRUSH;
  case DF_REFORM: return TB(MX_COLD);
  case DF_REFLECT: return M_PROJ;
  case DF_DARK: return M_LIGHT;
  default: return 0;
  }
}
uint16_t mx_mob_self_means(const mx_mob *m)
{
  uint16_t s = 0;
  if (m->atk == AT_PROJ || m->atk == AT_BEAM) s |= M_PROJ;
  return s;
}

uint32_t mx_mob_id(const mx_mob *m)
{
  return (uint32_t)m->body | ((uint32_t)m->atk << 4) | ((uint32_t)m->def << 7) |
         ((uint32_t)m->el << 10) | ((uint32_t)m->beh << 14);
}

static const uint8_t body_cost[BD_COUNT] = { 2, 3, 4, 3, 3, 2, 5, 4, 4, 3, 4 };
static const uint8_t atk_cost[AT_COUNT]  = { 1, 2, 2, 3, 2, 3, 2, 2 };
static const uint8_t def_cost[DF_COUNT]  = { 0, 2, 3, 2, 2, 3, 3, 2 };
static const uint8_t af_cost[AF_AURA0]   = { 2, 2, 2, 3, 2, 2 };
uint8_t mx_mob_cost(const mx_mob *m)
{
  uint8_t c = (uint8_t)(body_cost[m->body] + atk_cost[m->atk] + def_cost[m->def]), i;
  for (i = 0; i < 2; i++)
    if (m->af[i] != AF_NONE) c = (uint8_t)(c + (m->af[i] < AF_AURA0 ? af_cost[m->af[i]] : 2));
  return c;
}

/* ---------- names (CRUCIBLE voice: short, uppercase, odd) ---------- */
static const char *const adj_el[EL_COUNT][3] = {
  { "TOASTY", "SWEATY", "WARM" }, { "CHILLY", "NUMB", "FROSTY" }, { "DAMP", "SOGGY", "MOIST" },
  { "STATIC", "BUZZY", "ZAPPY" }, { "GLOSSY", "SHINY", "WAXED" }, { "LEAFY", "FURRY", "MOSSY" },
  { "HEXED", "WEIRD", "MINTY" }, { "WRONG", "NULL", "BENT" }, { "FIRM", "GRAVEL", "DENSE" },
  { "FLOATY", "BREEZY", "LIGHT" }, { "PLAIN", "BEIGE", "DULL" }
};
static const char *const adj_beh[BH_COUNT][2] = {
  { "SORRY", "SHY" }, { "ANGRY", "LOUD" }, { "SNEAKY", "SIDE" }, { "LOYAL", "STERN" },
  { "BOSSY", "HEAD" }, { "GRABBY", "SLY" }
};
static const char *const noun_biome[BI_COUNT][6] = {
  { "INTERN", "LAMP", "MEMO", "STAPLER", "CHAIR", "TEMP" },
  { "DUCT", "FAN", "FILTER", "RAT", "DRAFT", "GRILLE" },
  { "KOI", "PLANTER", "FERN", "FOUNTAIN", "FROG", "BENCH" },
  { "RACK", "CABLE", "FAN", "DIODE", "TICKET", "DRIVE" },
  { "PRETZEL", "MASCOT", "TRAY", "NAPKIN", "STRAW", "CART" },
  { "TULIP", "MOTH", "SWAN", "HEDGE", "GNOME", "PETAL" },
  { "PIGEON", "ANTENNA", "VANE", "GULL", "DISH", "KITE" },
  { "SPRITE", "PIXEL", "CURSOR", "ERROR", "TILE", "BYTE" }
};
static const char *const noun_body[BD_COUNT] = {
  0, "WING", "GHOST", "MOLE", "CREEP", "BLOB", "UNIT", "BLINK", "MITES", "TOWER", "BOX"
};
static uint8_t scpy(char *o, uint8_t at, const char *s)
{
  while (*s && at < 13) o[at++] = *s++;
  return at;
}
void mx_mob_name(const mx_mob *m, uint8_t biome, char *out)
{
  uint32_t h = mx_mob_id(m) * 2654435761u ^ ((uint32_t)biome << 3);
  const char *adj, *noun;
  uint8_t n;
  h ^= h >> 15;
  /* adjective from element or behaviour (both read as a voice), noun from biome or body */
  if ((h & 3) == 0) adj = adj_beh[m->beh][(h >> 2) & 1];
  else adj = adj_el[m->el][((h >> 3) & 0xFF) % 3];
  if (noun_body[m->body] && ((h >> 11) & 3) == 0) noun = noun_body[m->body];
  else noun = noun_biome[biome][((h >> 13) & 0xFF) % 6];
  n = scpy(out, 0, adj);
  if (n < 13) out[n++] = ' ';
  n = scpy(out, n, noun);
  out[n] = 0;
}

/* ---------- bosses ---------- */
const char *const mx_core_name[BC_COUNT] = { "COLOSSUS", "HIVE", "ENGINE", "SPECTER" };
const char *const mx_hz_name[HZ_COUNT] = { "collapse ring", "flooding", "darkness", "conveyor", "falling debris", "rising lava" };
static const uint8_t core_body[BC_COUNT] = { BD_HEAVY, BD_SWARM, BD_TURRET, BD_PHASER };
/* weak points a boss phase may use */
static const uint8_t boss_weak_ok[DF_COUNT] = { 0, 1, 1, 1, 0, 1, 1, 1 };

uint8_t mx_boss_valid(const mx_boss *b)
{
  uint8_t i, j, summon = 0;
  mx_mob probe;
  for (i = 0; i < 3; i++) {
    for (j = 0; j < i; j++) {
      if (b->atk[i] == b->atk[j]) return 0;
      if (b->weak[i] == b->weak[j]) return 0;
    }
    if (!boss_weak_ok[b->weak[i]]) return 0;
    /* phases speed up: tell * (4 - phase)/4 rounded must stay readable */
    if ((uint8_t)((mx_atk_tell[b->atk[i]] * (8 - i)) >> 3) < MIN_TELL) return 0;
    if (b->atk[i] == AT_SUMMON) summon = 1;
    probe.body = core_body[b->core]; probe.atk = b->atk[i]; probe.def = b->weak[i];
    probe.el = b->el[0]; probe.beh = BH_AGGRO;
    /* swarm-cored hives and turret engines are bigger than mobs: relax only the sprite rules */
    if (b->core == BC_HIVE) probe.body = BD_WALKER;
    if (b->core == BC_ENGINE) probe.beh = BH_GUARD;
    if (!mx_mob_valid(&probe)) return 0;
    if (b->weak[i] == DF_DARK && b->hazard != HZ_DARK) return 0;  /* dark weak point needs the dark hazard */
  }
  if (b->core == BC_HIVE && !summon) return 0;
  if (b->core == BC_ENGINE && (b->atk[0] == AT_LUNGE || b->atk[1] == AT_LUNGE || b->atk[2] == AT_LUNGE)) return 0;
  if (b->el[0] == b->el[1]) return 0;
  if (b->el[0] < MX_TAGS && b->el[1] < MX_TAGS && mx_rx_table[b->el[0]][b->el[1]] == 28) return 0; /* GROUND */
  if (b->hazard == HZ_LAVA && (b->el[0] == MX_COLD || b->el[1] == MX_COLD)) return 0;
  if (b->hazard == HZ_FLOOD && (b->weak[0] == DF_CRUSH || b->weak[1] == DF_CRUSH || b->weak[2] == DF_CRUSH)) return 0;
  return 1;
}

uint16_t mx_boss_means(const mx_boss *b)
{
  uint16_t m = 0;
  uint8_t i, shoots = 0;
  mx_mob probe;
  for (i = 0; i < 3; i++) {
    probe.def = b->weak[i];
    m |= mx_mob_means(&probe);
    if (b->atk[i] == AT_PROJ || b->atk[i] == AT_BEAM) shoots = 1;
  }
  if (shoots) m &= (uint16_t)~M_PROJ; /* it supplies its own shots */
  return m;
}
