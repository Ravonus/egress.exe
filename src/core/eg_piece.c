/* Pieces, recipes and the reaction table (see eg_piece.h). Shared verbatim by the cartridge and the host. */
#ifdef __SDCC
#pragma bank 255
#endif
#include "eg_piece.h"

/* 5 bytes: trigger:5|group:3, effect:5|shape:3, tags, magnitude:4|cooldown:4, flags */
#define PIECE(trg, grp, eff, sh, tags, mag, cd, fl)                                                                 \
  {(uint8_t)(((trg) << 3) | (grp)), (uint8_t)(((eff) << 3) | (sh)), (uint8_t)(tags), (uint8_t)(((mag) << 4) | (cd)), \
   (uint8_t)(fl)}
const uint8_t eg_piece[P_COUNT][EG_PIECE_BYTES] = {
    PIECE(0, 0, EFF_NONE, SH_SELF, 0, 0, 0, 0),                                              /* none */
    PIECE(TRG_USE, SG_STATUS, EFF_BURN, SH_TERRAIN, TG(TAG_HOT), 3, 4, PF_FOUND),             /* FIRE */
    PIECE(TRG_USE, SG_STATUS, EFF_WET, SH_TERRAIN, TG(TAG_WET), 2, 4, PF_FOUND),              /* WATER */
    PIECE(TRG_USE, SG_AREA, EFF_NONE, SH_AREA, 0, 1, 4, PF_FOUND),                           /* AIR (AIRY) */
    PIECE(TRG_USE, SG_DEFENSE, EFF_NONE, SH_TERRAIN, TG(TAG_STONE), 2, 4, PF_FOUND),          /* EARTH */
    PIECE(TRG_USE, SG_DEFENSE, EFF_NONE, SH_SELF, TG(TAG_SHINY) | TG(TAG_STONE), 2, 4, PF_FOUND), /* METAL */
    PIECE(TRG_USE, SG_REACT, EFF_NONE, SH_AURA, 0, 3, 4, PF_FOUND | PF_BENCH),               /* ENERGY (GLOWS) */
    PIECE(TRG_USE, SG_AREA, EFF_WET, SH_AREA, TG(TAG_WET), 2, 8, PF_FOUND | PF_BENCH),        /* RAIN */
    PIECE(TRG_USE, SG_SPEED, EFF_SLOW, SH_TERRAIN, TG(TAG_WET) | TG(TAG_STONE), 2, 8, PF_FOUND | PF_BENCH), /* MUD */
    PIECE(TRG_USE, SG_STATUS, EFF_FREEZE, SH_AREA, TG(TAG_COLD), 3, 8, PF_BENCH),            /* SNOW */
    PIECE(TRG_USE, SG_STATUS, EFF_FREEZE, SH_TERRAIN, TG(TAG_COLD) | TG(TAG_SHINY), 4, 8, PF_BENCH | PF_RARITY(1)),
    PIECE(TRG_USE, SG_DEFENSE, EFF_LAY, SH_TERRAIN, TG(TAG_STONE), 4, 0, PF_BENCH),          /* BRICK (MADE) */
    PIECE(TRG_USE, SG_STATUS, EFF_SHOCK, SH_TERRAIN, 0, 4, 4, PF_BENCH | PF_RARITY(1)),      /* ELECTRIC (GLOWS) */
    PIECE(TRG_USE, SG_STATUS, EFF_BLIND, SH_AREA, TG(TAG_HOT) | TG(TAG_WET), 2, 8, PF_BENCH), /* STEAM (AIRY) */
    PIECE(TRG_USE, SG_DAMAGE, EFF_BURN, SH_TERRAIN, TG(TAG_HOT) | TG(TAG_STONE), 4, 8, PF_BENCH), /* LAVA (GLOWS) */
    PIECE(TRG_USE, SG_STATUS, EFF_BLIND, SH_AREA, TG(TAG_STONE), 1, 8, PF_BENCH),            /* DUST (AIRY) */
    PIECE(TRG_USE, SG_AREA, EFF_WET, SH_AREA, TG(TAG_WET), 2, 8, PF_BENCH),                  /* CLOUD (AIRY) */
};

/* EGRESS's recipe subset for the office: the pairs follow CRUCIBLE's catalogue (docs/redesign.md section 3) */
const uint8_t eg_recipes[EG_RECIPES][3] = {
    {P_WATER, P_FIRE, P_STEAM}, {P_EARTH, P_WATER, P_MUD},  {P_EARTH, P_FIRE, P_LAVA},  {P_EARTH, P_AIR, P_DUST},
    {P_AIR, P_WATER, P_RAIN},   {P_AIR, P_FIRE, P_ENERGY},  {P_AIR, P_STEAM, P_CLOUD},  {P_AIR, P_RAIN, P_SNOW},
    {P_WATER, P_SNOW, P_ICE},   {P_FIRE, P_MUD, P_BRICK},   {P_ENERGY, P_METAL, P_ELECTRIC},
};

uint8_t eg_recipe(uint8_t a, uint8_t b) EG_BANKED {
  uint8_t k;
  for (k = 0u; k != EG_RECIPES; k++)
    if ((eg_recipes[k][0] == a && eg_recipes[k][1] == b) || (eg_recipes[k][0] == b && eg_recipes[k][1] == a))
      return eg_recipes[k][2];
  return 0u;
}

/* 5.2, symmetric */
const uint8_t eg_rx_table[EG_TAGS][EG_TAGS] = {
    /*          HOT          COLD           WET          SHINY       ALIVE         MAGIC          GLITCH          STONE */
    /* HOT */ {RX_FLARE, RX_THAW, RX_STEAM, RX_GLARE, RX_BURN, RX_EMBER, RX_FLICKER, RX_GLOWHEAT},
    /* COLD */ {RX_THAW, RX_DEEPFREEZE, RX_FREEZE, RX_PRISM, RX_FROSTBITE, RX_CRYSTAL, RX_STUTTER, RX_BRITTLE},
    /* WET */ {RX_STEAM, RX_FREEZE, RX_SOAK, RX_SLICK, RX_OVERGROW, RX_BLOOM, RX_SHORTWET, RX_MUD},
    /* SHINY */ {RX_GLARE, RX_PRISM, RX_SLICK, RX_REFLECT, RX_SUNFEED, RX_LENS, RX_GHOST, RX_POLISH},
    /* ALIVE */ {RX_BURN, RX_FROSTBITE, RX_OVERGROW, RX_SUNFEED, RX_SWARM, RX_WILD, RX_ROT, RX_MOSS},
    /* MAGIC */ {RX_EMBER, RX_CRYSTAL, RX_BLOOM, RX_LENS, RX_WILD, RX_RESONANCE, RX_GLITCHBURST, RX_RUNE},
    /* GLITCH */ {RX_FLICKER, RX_STUTTER, RX_SHORTWET, RX_GHOST, RX_ROT, RX_GLITCHBURST, RX_DUPLICATE, RX_NOCLIP},
    /* STONE */ {RX_GLOWHEAT, RX_BRITTLE, RX_MUD, RX_POLISH, RX_MOSS, RX_RUNE, RX_NOCLIP, RX_CRUSH},
};

/* B2: the table from any bank (fights read it through this) */
uint8_t eg_rx(uint8_t a, uint8_t b) EG_BANKED { return (a < EG_TAGS && b < EG_TAGS) ? eg_rx_table[a][b] : RX_NONE; }

uint8_t eg_tile_tag(uint8_t type) EG_BANKED {
  switch (type) {
    case T_PAPER:
    case T_PLANK:
      return TAG_ALIVE; /* paper, posters, cardboard: wood */
    case T_WATER:
      return TAG_WET;
    case T_ICE:
    case T_ICE_1:
      return TAG_COLD;
    default:
      return EG_TAGS;
  }
}

static uint8_t q[EG_N];
static uint8_t seen[EG_N / 8u];

static void mark(uint8_t i) {
  if (eg_ndirty < EG_DIRTY_MAX) eg_dirty[eg_ndirty] = i;
  if (eg_ndirty != 255u) eg_ndirty++;
}

static uint8_t in_occ(uint8_t i, const uint8_t *occ, uint8_t n) {
  while (n) {
    n--;
    if (occ[n] == i) return 1u;
  }
  return 0u;
}

/* what a spread may enter */
static uint8_t spread_class(uint8_t rx, uint8_t v) {
  if (rx == RX_BURN) return (uint8_t)(v == T_PAPER || v == T_PLANK);
  if (rx == RX_THAW) return (uint8_t)(v == T_ICE || v == T_ICE_1);
  return (uint8_t)(v == T_WATER); /* FREEZE, SHORT */
}

static const int8_t step4[4] = {-16, 16, -1, 1};

/* breadth-first over the connected cells of the spread's class, from `cell`, skipping the player's cells; the group
 * lands in q[0..n) nearest first */
static uint8_t spread(const eg_room *r, uint8_t rx, uint8_t cell, const uint8_t *occ, uint8_t nocc) {
  uint8_t n = 0u, h = 0u, d, s, x, y, i;
  for (i = 0u; i != (uint8_t)(EG_N / 8u); i++) seen[i] = 0u;
  q[n++] = cell;
  seen[cell >> 3] |= (uint8_t)(1u << (cell & 7u));
  while (h != n) {
    i = q[h++];
    x = EG_X(i);
    y = EG_Y(i);
    for (d = 0u; d != 4u; d++) {
      if ((d == 0u && y == 0u) || (d == 1u && y == EG_H - 1u) || (d == 2u && x == 0u) || (d == 3u && x == EG_W - 1u))
        continue;
      s = (uint8_t)(i + step4[d]);
      if (seen[s >> 3] & (uint8_t)(1u << (s & 7u))) continue;
      seen[s >> 3] |= (uint8_t)(1u << (s & 7u));
      if (!spread_class(rx, r->c[s]) || in_occ(s, occ, nocc)) continue;
      q[n++] = s;
    }
  }
  return n;
}

/* a shock reached these cells: power every socket touching them; with one powered, every dead shutter goes */
static uint8_t power(eg_room *r, uint8_t n) {
  uint8_t k, d, s, i, x, y, lit = 0u;
  for (k = 0u; k != n; k++) {
    i = q[k];
    mark(i);
    if (r->c[i] == T_SOCKET) {
      r->c[i] = T_SOCKET_ON;
      lit = 1u;
      continue;
    }
    x = EG_X(i);
    y = EG_Y(i);
    for (d = 0u; d != 4u; d++) {
      if ((d == 0u && y == 0u) || (d == 1u && y == EG_H - 1u) || (d == 2u && x == 0u) || (d == 3u && x == EG_W - 1u))
        continue;
      s = (uint8_t)(i + step4[d]);
      if (r->c[s] == T_SOCKET) {
        r->c[s] = T_SOCKET_ON;
        mark(s);
        lit = 1u;
      }
    }
  }
  if (!lit) return 0u;
  for (i = 0u; i != (uint8_t)EG_N; i++)
    if (r->c[i] == T_DEAD) {
      r->c[i] = T_EMPTY;
      mark(i);
    }
  return 1u;
}

uint8_t eg_apply(eg_room *r, uint8_t piece, uint8_t cell, const uint8_t *occ, uint8_t nocc) EG_BANKED {
  uint8_t v = r->c[cell], eff, tags, tt, k, rx, n;
  eg_ndirty = 0u;
  eg_plates_dirty = 1u; /* a brick may land on a plate, or burn off one */
  if (!piece || piece >= P_COUNT || in_occ(cell, occ, nocc)) return RX_NONE;
  eff = PC_EFF(piece);
  tags = PC_TAGS(piece);
  if (eff == EFF_LAY) {
    if (v == T_EMPTY) r->c[cell] = T_BLOCK;
    else if (v == T_PLATE) r->c[cell] = T_BLOCK_PLATE;
    else return RX_NONE;
    mark(cell);
    return RX_WEIGHT;
  }
  if (eff == EFF_SHOCK) {
    if (v == T_SOCKET) {
      q[0] = cell;
      n = 1u;
    } else if (v == T_WATER)
      n = spread(r, RX_SHORT, cell, occ, nocc);
    else
      return RX_NONE;
    eg_ndirty = 0u;
    return power(r, n) ? RX_SHORT : RX_NONE;
  }
  tt = eg_tile_tag(v);
  if (tt == EG_TAGS) return RX_NONE;
  for (k = 0u; k != EG_TAGS; k++) {
    if (!(tags & TG(k))) continue;
    rx = eg_rx_table[k][tt];
    if ((rx != RX_BURN && rx != RX_FREEZE && rx != RX_THAW) || !spread_class(rx, v)) continue;
    n = spread(r, rx, cell, occ, nocc);
    for (k = 0u; k != n; k++) {
      v = q[k];
      if (rx == RX_BURN) r->c[v] = r->c[v] == T_PLANK ? T_PIT : T_EMPTY;
      else if (rx == RX_FREEZE) r->c[v] = T_ICE; /* thin ice: fragile, 2 steps */
      else r->c[v] = T_WATER;
      mark(v);
    }
    return rx;
  }
  return RX_NONE;
}

/* --- names --- */
static const char piece_names[P_COUNT][9] = {"",     "FIRE", "WATER", "AIR",      "EARTH", "METAL",
                                             "ENERGY", "RAIN", "MUD", "SNOW",     "ICE",   "BRICK",
                                             "ELECTRIC", "STEAM", "LAVA", "DUST", "CLOUD"};
static const char piece_abbr[P_COUNT][5] = {"    ", "FIRE", "WATR", "AIR ", "ERTH", "METL", "ENGY", "RAIN", "MUD ",
                                            "SNOW", "ICE ", "BRIK", "ELEC", "STEM", "LAVA", "DUST", "CLOD"};
static const char rx_names[RX_COUNT][14] = {
    "",         "FLARE",     "THAW",       "STEAM",     "GLARE",     "BURN",      "EMBER",     "FLICKER",
    "GLOW HEAT", "DEEP FREEZE", "FREEZE",  "PRISM",     "FROSTBITE", "CRYSTAL",   "STUTTER",   "BRITTLE",
    "SOAK",     "SLICK",     "OVERGROW",   "BLOOM",     "SHORT",     "MUD",       "REFLECT",   "SUN FEED",
    "LENS",     "GHOST",     "POLISH",     "SWARM",     "WILD GROWTH", "ROT",     "MOSS",      "RESONANCE",
    "GLITCH",   "RUNE",      "DUPLICATE",  "NOCLIP",    "CRUSH",     "SHORT CIRCUIT", "WEIGHT"};
static const char tag_names[EG_TAGS][7] = {"HOT", "COLD", "WET", "SHINY", "ALIVE", "MAGIC", "GLITCH", "STONE"};

static void copy(char *out, const char *s, uint8_t max) {
  while (*s && max--) *out++ = *s++;
  *out = 0;
}

void eg_piece_name(uint8_t piece, char *out) EG_BANKED { copy(out, piece < P_COUNT ? piece_names[piece] : "?", 8u); }
void eg_piece_abbr(uint8_t piece, char *out) EG_BANKED { copy(out, piece_abbr[piece < P_COUNT ? piece : 0u], 4u); }
void eg_rx_name(uint8_t rx, char *out) EG_BANKED { copy(out, rx < RX_COUNT ? rx_names[rx] : "?", 13u); }

/* the traits as the bench shows them: the tag byte, plus what the effect means where a tag can't say it */
void eg_tags_text(uint8_t piece, char *out) EG_BANKED {
  uint8_t k, n = 0u, tags = piece < P_COUNT ? PC_TAGS(piece) : 0u, eff = piece < P_COUNT ? PC_EFF(piece) : 0u;
  const char *s;
  for (k = 0u; k != EG_TAGS; k++) {
    if (!(tags & TG(k))) continue;
    if (n && n < 17u) out[n++] = ' ';
    for (s = tag_names[k]; *s && n < 18u; s++) out[n++] = *s;
  }
  s = eff == EFF_SHOCK ? "SHOCK" : (eff == EFF_LAY ? "WEIGHT" : (n ? "" : "-"));
  if (*s && n && n < 17u) out[n++] = ' ';
  for (; *s && n < 18u; s++) out[n++] = *s;
  out[n] = 0;
}

uint8_t eg_piece_tags(uint8_t piece) EG_BANKED { return piece < P_COUNT ? PC_TAGS(piece) : 0u; }
