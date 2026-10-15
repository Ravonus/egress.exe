/* Piece schema, stacking, rng and the reaction matrix (one table for combat and tiles). */
#include "mx.h"

const char *const mx_tag_name[MX_TAGS] = {
  "HOT", "COLD", "WET", "SHOCK", "SHINY", "ALIVE", "MAGIC", "GLITCH", "STONE", "AIRY"
};
const char *const mx_trig_name[TR_COUNT] = {
  "on hit", "on dodge", "on parry", "on kill", "on charge release", "on low HP", "on push",
  "on recipe made", "every N s", "on hurt", "on enter tile", "passive"
};
const char *const mx_eff_name[EF_COUNT] = {
  "projectile", "area", "status", "terrain change", "stat change", "summon", "trail", "beam"
};
const char *const mx_kind_name[PK_COUNT] = {
  "weapon base", "weapon mod", "heir trait", "dodge style", "mob part", "elite affix", "boss part", "element"
};
const char *const mx_group_name[SG_COUNT] = {
  "DAMAGE", "STATUS", "AREA", "PROJ", "DEFENSE", "SPEED", "SUMMON", "REACT"
};

/* ---------- rng: xorshift32 ---------- */
void mx_seed(mx_rng *r, uint32_t seed) { r->s = seed ? seed : 0x2545F491u; mx_rand(r); mx_rand(r); }
uint16_t mx_rand(mx_rng *r)
{
  uint32_t x = r->s;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  r->s = x;
  return (uint16_t)(x >> 8);
}
uint8_t mx_below(mx_rng *r, uint8_t n)
{
  if (n == 0) return 0;
  return (uint8_t)(((uint32_t)mx_rand(r) * n) >> 16);
}
uint8_t mx_pick(mx_rng *r, const uint8_t *w, uint8_t n)
{
  uint16_t tot = 0, x;
  uint8_t i;
  for (i = 0; i < n; i++) tot = (uint16_t)(tot + w[i]);
  if (!tot) return mx_below(r, n);
  x = (uint16_t)(((uint32_t)mx_rand(r) * tot) >> 16);
  for (i = 0; i < n; i++) {
    if (x < w[i]) return i;
    x = (uint16_t)(x - w[i]);
  }
  return (uint8_t)(n - 1);
}

/* ---------- stacking ---------- */
/* soft caps per group (percent units). Tuned by sim.c; ship as const. */
uint8_t mx_cap[SG_COUNT] = { 90, 70, 80, 80, 45, 40, 60, 60 };
uint8_t mx_dr_on = 1;
static const uint8_t dr_step[8] = { 16, 12, 8, 6, 4, 3, 2, 2 };

uint16_t mx_stack(const uint8_t *mag, uint8_t n, uint8_t group)
{
  uint8_t s[8], i, j, t;
  uint16_t tot = 0, cap, hard;
  if (n > 8) n = 8;
  for (i = 0; i < n; i++) s[i] = mag[i];
  for (i = 1; i < n; i++) { /* insertion sort, descending */
    t = s[i]; j = i;
    while (j && s[j - 1] < t) { s[j] = s[j - 1]; j--; }
    s[j] = t;
  }
  if (!mx_dr_on) {
    for (i = 0; i < n; i++) tot = (uint16_t)(tot + s[i]);
    return tot;
  }
  for (i = 0; i < n; i++) tot = (uint16_t)(tot + ((s[i] * dr_step[i]) >> 4));
  cap = mx_cap[group];
  hard = (uint16_t)(cap + (cap >> 1));
  if (tot > cap) tot = (uint16_t)(cap + ((tot - cap) >> 2));
  if (tot > hard) tot = hard;
  return tot;
}

/* ---------- tiles ---------- */
const char *const mx_tile_name[TL_COUNT] = {
  "floor", "water", "ice", "pit", "wall", "cracked wall", "vines", "mud", "lava", "mirror",
  "vent", "power node", "crate", "lamp", "rift", "steam", "plant bridge", "snow", "lit", "dart trap"
};
const uint8_t mx_tile_tag[TL_COUNT] = {
  MX_NOTAG, MX_WET, MX_COLD, MX_NOTAG, MX_STONE, MX_STONE, MX_ALIVE, MX_WET, MX_HOT, MX_SHINY,
  MX_AIRY, MX_SHOCK, MX_NOTAG, MX_HOT, MX_GLITCH, MX_HOT, MX_ALIVE, MX_COLD, MX_SHINY, MX_NOTAG
};

/* ---------- reactions ---------- */
#define R(n, k, m, c, d, ti, to, note) { n, k, m, c, d, ti, to, note }
const mx_reaction mx_rx[RX_COUNT] = {
  R("-",          RK_NONE,    16,  0,  0, TL_FLOOR, TL_KEEP,  "-"),
  R("STEAM",      RK_BLIND,   18, 45, 12, TL_WATER, TL_STEAM, "water boils to a blinding cloud; vents jam"),
  R("SHATTER",    RK_BURST,   40,  0,  0, TL_ICE,   TL_WATER, "ice path melts; frozen foes burst x2.5"),
  R("FREEZE",     RK_ROOT,    16, 70,  8, TL_WATER, TL_ICE,   "water freezes into a sliding ice path"),
  R("SHORT",      RK_STUN,    20, 80,  6, TL_WATER, TL_KEEP,  "water carries the shock; powers nodes, chains stun"),
  R("OVERGROW",   RK_ROOT,    16, 60, 10, TL_PIT,   TL_BRIDGE,"vines bridge a pit or water; foes rooted"),
  R("BURN",       RK_DOT,     24, 10, 16, TL_VINE,  TL_FLOOR, "vines/hedges burn away; spreads to ALIVE"),
  R("MELT",       RK_ARMOR,   22,  0, 12, TL_CRACK, TL_LAVA,  "cracked wall melts to cooling lava (opens later)"),
  R("FIRESTORM",  RK_DOT,     26, 20, 10, TL_VENT,  TL_KEEP,  "vent throws a fire jet; pushes"),
  R("OVERLOAD",   RK_BURST,   36,  0,  0, TL_NODE,  TL_FLOOR, "node blows: door opens, blast radius 2"),
  R("LANTERN",    RK_LIGHT,   16, 15, 32, TL_MIRROR,TL_LIGHT, "mirror heats into a light source; reveals"),
  R("CINDER",     RK_BURST,   30,  0,  0, TL_FLOOR, TL_KEEP,  "delayed mark explodes after 1 s"),
  R("DEEPFREEZE", RK_ROOT,    16, 90, 16, TL_WATER, TL_ICE,   "hard freeze; stops reforming foes"),
  R("BRITTLE",    RK_ARMOR,   28,  0, 12, TL_ICE,   TL_KEEP,  "ice conducts; next hit on target crits"),
  R("ICE MIRROR", RK_REFLECT, 16, 20, 16, TL_FLOOR, TL_MIRROR,"raises a reflective ice pane"),
  R("WILT",       RK_SLOW,    16, 35, 16, TL_VINE,  TL_FLOOR, "vines go brittle and break; stops regrowth"),
  R("STASIS",     RK_STUN,    16, 50,  8, TL_FLOOR, TL_KEEP,  "orbs in flight stop; platform freezes"),
  R("CRACK",      RK_ARMOR,   20,  0, 12, TL_WALL,  TL_CRACK, "frost cracks a wall (breakable)"),
  R("SNOW",       RK_REVEAL,  16, 30, 24, TL_FLOOR, TL_SNOW,  "snow slows and shows invisible footprints"),
  R("FLOOD",      RK_SLOW,    16, 20, 16, TL_PIT,   TL_WATER, "water spreads; fills a pit"),
  R("MIRAGE",     RK_CONFUSE, 16, 40, 12, TL_WATER, TL_LIGHT, "puddle reflects light; decoy draws attacks"),
  R("DISPEL",     RK_DISPEL,  16, 25, 16, TL_RIFT,  TL_FLOOR, "strips a shield/affix for 4 s; closes rifts"),
  R("MUD",        RK_SLOW,    16, 35, 16, TL_FLOOR, TL_MUD,   "ground turns to mud; burrowers surface"),
  R("SQUALL",     RK_PUSH,    18, 25,  4, TL_CRATE, TL_KEEP,  "wave pushes crates one cell"),
  R("OVERCHARGE", RK_CHAIN,   22, 30,  6, TL_NODE,  TL_KEEP,  "node stays powered longer; +1 chain"),
  R("LASER",      RK_BURST,   34,  0,  0, TL_MIRROR,TL_KEEP,  "mirror relays a piercing beam"),
  R("SPASM",      RK_STUN,    18, 60,  4, TL_VINE,  TL_KEEP,  "vines twitch open for 2 s; foes interrupted"),
  R("ARC",        RK_CHAIN,   26, 30,  6, TL_FLOOR, TL_KEEP,  "chain hits 3 targets"),
  R("GROUND",     RK_NULL,    12,  0,  0, TL_WALL,  TL_KEEP,  "stone eats the shock (anti-synergy)"),
  R("THUNDER",    RK_STUN,    22, 55,  6, TL_VENT,  TL_KEEP,  "stun ring radius 2"),
  R("MIRROR",     RK_SPLIT,   24,  0,  0, TL_MIRROR,TL_KEEP,  "double reflect; shots split in 2"),
  R("BLOOM",      RK_HEAL,    16,  0, 16, TL_VINE,  TL_BRIDGE,"light grows vines into a bridge; heal pulse"),
  R("PRISM",      RK_SPLIT,   26,  0,  0, TL_MIRROR,TL_KEEP,  "shot splits into 3"),
  R("GLINT",      RK_REFLECT, 16, 10,  0, TL_WALL,  TL_MIRROR,"polished wall becomes a reflector"),
  R("DAZZLE",     RK_CONFUSE, 16, 45, 10, TL_FLOOR, TL_KEEP,  "foes swing at each other"),
  R("TREE",       RK_WALL,    16, 30, 32, TL_FLOOR, TL_VINE,  "sprouts a tree: cover / blocker"),
  R("FAMILIAR",   RK_SUMMON,  20, 15, 40, TL_FLOOR, TL_KEEP,  "a small ally sprite fights 10 s"),
  R("ROOTBREAK",  RK_ARMOR,   24,  0, 12, TL_WALL,  TL_CRACK, "roots crack stone"),
  R("POLLEN",     RK_STUN,    16, 65, 12, TL_FLOOR, TL_KEEP,  "sleep cloud; breaks on hit"),
  R("SURGE",      RK_BURST,   26,  0,  0, TL_RIFT,  TL_KEEP,  "magnitude up; rift widens"),
  R("GOLEM",      RK_SUMMON,  18, 25, 32, TL_CRATE, TL_KEEP,  "animates a crate: it walks 3 cells"),
  R("VORTEX",     RK_PULL,    18, 35,  6, TL_CRATE, TL_KEEP,  "pulls foes/crates together"),
  R("WALL",       RK_WALL,    16, 25, 32, TL_FLOOR, TL_CRATE, "raises rubble (pushable)"),
  R("SANDSTORM",  RK_BLIND,   18, 35, 12, TL_FLOOR, TL_KEEP,  "blind + chip damage"),
  R("GUST",       RK_PUSH,    16, 30,  4, TL_CRATE, TL_KEEP,  "strong knockback; slides crates to a wall"),
  R("GLITCH",     RK_RANDOM,  24, 20,  4, TL_FLOOR, TL_RIFT,  "plays a seeded reaction of the partner tag + 1 frame stutter"),
  R("CRASH",      RK_STUN,    16, 70,  8, TL_FLOOR, TL_RIFT,  "room stutter: all foes freeze 2 s, two tiles swap"),
  R("FLARE",      RK_DOT,     22,  0, 16, TL_LAMP,  TL_KEEP,  "burn stacks deeper; lamp flares"),
  R("REFLECT",    RK_REFLECT, 20,  0,  0, TL_MIRROR,TL_KEEP,  "SHINY + any projectile: shot reflects (or splits if already reflected)"),
};
#undef R

/* symmetric tag x tag matrix (100 bytes; a 55-byte triangle in ROM) */
enum {
  r0, rSTEAM, rSHATTER, rFREEZE, rSHORT, rOVERGROW, rBURN, rMELT, rFIRESTORM, rOVERLOAD,
  rLANTERN, rCINDER, rDEEP, rBRITTLE, rICEMIR, rWILT, rSTASIS, rCRACK, rSNOW, rFLOOD,
  rMIRAGE, rDISPEL, rMUD, rSQUALL, rOVERCH, rLASER, rSPASM, rARC, rGROUND, rTHUNDER,
  rMIRROR, rBLOOM, rPRISM, rGLINT, rDAZZLE, rTREE, rFAMIL, rROOTB, rPOLLEN, rSURGE,
  rGOLEM, rVORTEX, rWALL, rSAND, rGUST, rGLITCH, rCRASH, rFLARE
};
const uint8_t mx_rx_table[MX_TAGS][MX_TAGS] = {
  /*            HOT       COLD      WET      SHOCK     SHINY    ALIVE    MAGIC    GLITCH   STONE    AIRY */
  /* HOT   */ { rFLARE,   rSHATTER, rSTEAM,  rOVERLOAD,rLANTERN,rBURN,   rCINDER, rGLITCH, rMELT,   rFIRESTORM },
  /* COLD  */ { rSHATTER, rDEEP,    rFREEZE, rBRITTLE, rICEMIR, rWILT,   rSTASIS, rGLITCH, rCRACK,  rSNOW },
  /* WET   */ { rSTEAM,   rFREEZE,  rFLOOD,  rSHORT,   rMIRAGE, rOVERGROW,rDISPEL,rGLITCH, rMUD,    rSQUALL },
  /* SHOCK */ { rOVERLOAD,rBRITTLE, rSHORT,  rOVERCH,  rLASER,  rSPASM,  rARC,    rGLITCH, rGROUND, rTHUNDER },
  /* SHINY */ { rLANTERN, rICEMIR,  rMIRAGE, rLASER,   rMIRROR, rBLOOM,  rPRISM,  rGLITCH, rGLINT,  rDAZZLE },
  /* ALIVE */ { rBURN,    rWILT,    rOVERGROW,rSPASM,  rBLOOM,  rTREE,   rFAMIL,  rGLITCH, rROOTB,  rPOLLEN },
  /* MAGIC */ { rCINDER,  rSTASIS,  rDISPEL, rARC,     rPRISM,  rFAMIL,  rSURGE,  rGLITCH, rGOLEM,  rVORTEX },
  /* GLITCH*/ { rGLITCH,  rGLITCH,  rGLITCH, rGLITCH,  rGLITCH, rGLITCH, rGLITCH, rCRASH,  rGLITCH, rGLITCH },
  /* STONE */ { rMELT,    rCRACK,   rMUD,    rGROUND,  rGLINT,  rROOTB,  rGOLEM,  rGLITCH, rWALL,   rSAND },
  /* AIRY  */ { rFIRESTORM,rSNOW,   rSQUALL, rTHUNDER, rDAZZLE, rPOLLEN, rVORTEX, rGLITCH, rSAND,   rGUST },
};

/* Fixed resolution order: (1) table lookup, (2) GLITCH re-roll from partner row (seeded),
 * (3) SHINY+projectile adds REFLECT as a second result. Never recursive. */
uint8_t mx_react(uint8_t a, uint8_t b, uint8_t ctx, uint16_t seed, uint8_t *extra)
{
  uint8_t rx, partner, t;
  if (extra) *extra = RX_NONE;
  if (a >= MX_TAGS || b >= MX_TAGS) {
    if (extra && (ctx & CTX_PROJ) && (a == MX_SHINY || b == MX_SHINY)) *extra = RX_REFLECT;
    return RX_NONE;
  }
  rx = mx_rx_table[a][b];
  if (rx == rGLITCH) {
    partner = (a == MX_GLITCH) ? b : a;
    t = (uint8_t)(seed % (MX_TAGS - 1));
    if (t >= MX_GLITCH) t++;          /* never GLITCH again: no recursion */
    rx = mx_rx_table[partner][t];
    if (rx == rGLITCH || rx == r0) rx = rCRASH;
  }
  if (extra && (ctx & CTX_PROJ) && (a == MX_SHINY || b == MX_SHINY) && rx != rMIRROR && rx != rPRISM)
    *extra = RX_REFLECT;
  return rx;
}

/* the same table drives tiles: a tile reacts through its intrinsic tag */
uint8_t mx_tile_react(uint8_t tile, uint8_t tag, uint16_t seed, uint8_t *rx_out)
{
  uint8_t tt = mx_tile_tag[tile], rx;
  const mx_reaction *r;
  *rx_out = RX_NONE;
  if (tt == MX_NOTAG) {
    /* open tiles: only reactions that create terrain from nothing (self pairs, e.g. COLD+AIRY snow) */
    if (tile == TL_FLOOR || tile == TL_PIT || tile == TL_CRATE) {
      uint8_t i;
      for (i = 0; i < MX_TAGS; i++) {
        rx = mx_rx_table[tag][i];
        r = &mx_rx[rx];
        if (r->tile_in == tile && r->tile_out != TL_KEEP && i == tag) { *rx_out = rx; return r->tile_out; }
      }
    }
    return TL_KEEP;
  }
  rx = mx_react(tt, tag, 0, seed, 0);
  *rx_out = rx;
  r = &mx_rx[rx];
  if (r->tile_in == tile || r->tile_in == TL_FLOOR) return r->tile_out;
  return TL_KEEP;
}
