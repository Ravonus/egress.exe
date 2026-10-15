/* Player builds (weapon base + 2 mods + 2 heir traits + dodge) and a crude abstract combat model.
 * This is NOT the game's combat: it is a budget model (time-to-kill x incoming damage) used to find
 * runaway stacks and dead pieces before they are built. Integer only. */
#include "mx_build.h"

#define P(tr, ef, ta, tb, mag, aa, ab, kind, grp, cap) MXP(tr, ef, ta, tb, mag, aa, ab, kind, grp, cap)
#define N MX_NOTAG

const mx_wbase mx_wbases[WB_COUNT] = {
  /* name      dps aoe reach parry pierce push proj */
  { "BLADE",    11, 1,  2, 1, 0, 0, 0, P(TR_HIT, EF_STAT, N, N, 0, 3, 0, PK_WBASE, SG_DAMAGE, 0) },
  { "HAMMER",   12, 2,  0, 0, 1, 2, 0, P(TR_CHARGE, EF_AREA, MX_STONE, N, 20, 2, 0, PK_WBASE, SG_AREA, 0) },
  { "SPEAR",     9, 1,  8, 1, 2, 0, 0, P(TR_HIT, EF_STAT, N, N, 0, 1, 0, PK_WBASE, SG_DAMAGE, 0) },
  { "DISC",      8, 1,  8, 0, 0, 0, 1, P(TR_HIT, EF_PROJ, N, N, 10, 1, 0, PK_WBASE, SG_PROJ, 0) },
  { "WAND",      7, 1,  9, 0, 0, 0, 2, P(TR_HIT, EF_PROJ, MX_MAGIC, N, 15, 1, 0, PK_WBASE, SG_PROJ, 0) },
  { "GAUNTLET", 11, 0,  2, 3, 1, 1, 0, P(TR_PARRY, EF_STAT, N, N, 20, 0, 4, PK_WBASE, SG_DAMAGE, 0) },
};

const mx_named_piece mx_mods[N_MODS] = {
  { "EMBER EDGE",   P(TR_HIT,     EF_STATUS,  MX_HOT,    N, 30, 0, 8, PK_WMOD, SG_STATUS, 0) },
  { "FROST CORE",   P(TR_CHARGE,  EF_AREA,    MX_COLD,   N, 40, 2, 6, PK_WMOD, SG_AREA, 0) },
  { "TIDE HEM",     P(TR_DODGE,   EF_AREA,    MX_WET,    N, 30, 1, 8, PK_WMOD, SG_AREA, 0) },
  { "SPARK PLUG",   P(TR_HIT,     EF_STATUS,  MX_SHOCK,  N, 30, 3, 2, PK_WMOD, SG_STATUS, 0) },
  { "MIRROR GUARD", P(TR_PARRY,   EF_PROJ,    MX_SHINY,  N, 50, 2, 0, PK_WMOD, SG_PROJ, 0) },
  { "SEED POUCH",   P(TR_KILL,    EF_SUMMON,  MX_ALIVE,  N, 40, 1, 15, PK_WMOD, SG_SUMMON, 0) },
  { "HEX INK",      P(TR_HIT,     EF_STATUS,  MX_MAGIC,  N, 28, 0, 8, PK_WMOD, SG_STATUS, 0) },
  { "BAD SECTOR",   P(TR_EVERY,   EF_AREA,    MX_GLITCH, N, 35, 4, 2, PK_WMOD, SG_AREA, 0) },
  { "GRAVEL CORE",  P(TR_PUSH,    EF_TERRAIN, MX_STONE,  N, 60, 1, 0, PK_WMOD, SG_DAMAGE, 0) },
  { "FAN BLADE",    P(TR_DODGE,   EF_PROJ,    MX_AIRY,   N, 30, 3, 0, PK_WMOD, SG_PROJ, 0) },
  { "LENS",         P(TR_CHARGE,  EF_BEAM,    MX_SHINY,  N, 45, 1, 2, PK_WMOD, SG_PROJ, 0) },
  { "KILN",         P(TR_LOWHP,   EF_STAT,    MX_HOT,    N, 70, 0, 15, PK_WMOD, SG_DAMAGE, 1) },
  { "WHETSTONE",    P(TR_PASSIVE, EF_STAT,    N,         N, 35, 0, 0, PK_WMOD, SG_DAMAGE, 0) },
  { "LUCKY CABLE",  P(TR_HURT,    EF_AREA,    MX_SHOCK,  N, 40, 2, 2, PK_WMOD, SG_AREA, 0) },
  { "MOSS GRIP",    P(TR_HIT,     EF_STAT,    MX_ALIVE,  N, 20, 0, 0, PK_WMOD, SG_DEFENSE, 0) },
  { "PRISM CUT",    P(TR_HIT,     EF_PROJ,    MX_SHINY, MX_MAGIC, 22, 3, 0, PK_WMOD, SG_PROJ, 0) },
  { "BRINE TIP",    P(TR_HIT,     EF_STATUS,  MX_WET,    N, 28, 0, 8, PK_WMOD, SG_STATUS, 0) },
  { "RECIPE BELL",  P(TR_RECIPE,  EF_AREA,    MX_MAGIC,  N, 60, 3, 4, PK_WMOD, SG_AREA, 1) },
};

/* cost pieces: cap class 3. group meaning for costs: DAMAGE=-off, DEFENSE=-def (more damage taken),
 * SPEED=-avoid, SUMMON=healing cut */
const mx_trait mx_traits[N_TRAITS] = {
  { "GLITCHED",      P(TR_DODGE,   EF_TRAIL,  MX_GLITCH, N, 35, 0, 6, PK_TRAIT, SG_AREA, 0),
                     P(TR_PASSIVE, EF_STAT,   N, N, 8, 0, 0, PK_TRAIT, SG_SPEED, CAP_COST), "inputs stutter 1 frame in 20" },
  { "WET HANDS",     P(TR_HIT,     EF_STATUS, MX_WET, N, 25, 0, 8, PK_TRAIT, SG_STATUS, 0),
                     P(TR_HURT,    EF_STAT,   MX_SHOCK, N, 12, 0, 0, PK_TRAIT, SG_DEFENSE, CAP_COST), "shock hurts +25%" },
  { "HOTHEAD",       P(TR_CHARGE,  EF_AREA,   MX_HOT, N, 35, 1, 4, PK_TRAIT, SG_AREA, 0),
                     P(TR_LOWHP,   EF_STAT,   N, N, 10, 0, 0, PK_TRAIT, SG_DEFENSE, CAP_COST), "low-HP threshold is higher" },
  { "BRITTLE",       P(TR_DODGE,   EF_STAT,   N, N, 15, 0, 2, PK_TRAIT, SG_SPEED, 0),
                     P(TR_PASSIVE, EF_STAT,   N, N, 15, 0, 0, PK_TRAIT, SG_DEFENSE, CAP_COST), "takes +20% damage" },
  { "GREEN THUMB",   P(TR_KILL,    EF_SUMMON, MX_ALIVE, N, 30, 1, 12, PK_TRAIT, SG_SUMMON, 0),
                     P(TR_PASSIVE, EF_STAT,   N, N, 6, 0, 0, PK_TRAIT, SG_SPEED, CAP_COST), "walks slower" },
  { "SHINY TEETH",   P(TR_PARRY,   EF_STAT,   MX_SHINY, N, 30, 0, 2, PK_TRAIT, SG_PROJ, 0),
                     P(TR_TILE,    EF_STAT,   N, N, 5, 0, 0, PK_TRAIT, SG_DEFENSE, CAP_COST), "glows: seen in dark rooms" },
  { "GIANT",         P(TR_PASSIVE, EF_STAT,   MX_STONE, N, 30, 0, 0, PK_TRAIT, SG_DEFENSE, 0),
                     P(TR_PASSIVE, EF_STAT,   N, N, 10, 0, 0, PK_TRAIT, SG_SPEED, CAP_COST), "bigger hitbox" },
  { "TINY",          P(TR_PASSIVE, EF_STAT,   MX_AIRY, N, 8, 0, 0, PK_TRAIT, SG_SPEED, 0),
                     P(TR_PASSIVE, EF_STAT,   N, N, 20, 0, 0, PK_TRAIT, SG_DAMAGE, CAP_COST), "-20% damage" },
  { "FROSTBITTEN",   P(TR_HIT,     EF_STATUS, MX_COLD, N, 25, 0, 8, PK_TRAIT, SG_STATUS, 0),
                     P(TR_PASSIVE, EF_STAT,   N, N, 10, 0, 0, PK_TRAIT, SG_DEFENSE, CAP_COST), "-10 max HP" },
  { "STATIC HAIR",   P(TR_HURT,    EF_AREA,   MX_SHOCK, N, 35, 2, 2, PK_TRAIT, SG_AREA, 0),
                     P(TR_TILE,    EF_STAT,   MX_WET, N, 8, 0, 0, PK_TRAIT, SG_DEFENSE, CAP_COST), "water tiles sting" },
  { "SLEEPWALKER",   P(TR_EVERY,   EF_STAT,   MX_MAGIC, N, 40, 6, 8, PK_TRAIT, SG_DAMAGE, 0),
                     P(TR_PASSIVE, EF_STAT,   N, N, 6, 0, 0, PK_TRAIT, SG_SPEED, CAP_COST), "drifts while idle" },
  { "BERSERK",       P(TR_LOWHP,   EF_STAT,   N, N, 80, 0, 0, PK_TRAIT, SG_DAMAGE, 1),
                     P(TR_PASSIVE, EF_STAT,   N, N, 30, 0, 0, PK_TRAIT, SG_SUMMON, CAP_COST), "heals 30% less" },
  { "STONE SKIN",    P(TR_PASSIVE, EF_STAT,   MX_STONE, N, 25, 0, 0, PK_TRAIT, SG_DEFENSE, 0),
                     P(TR_PASSIVE, EF_STAT,   N, N, 10, 0, 0, PK_TRAIT, SG_SPEED, CAP_COST), "heavy: shorter dodge" },
  { "FEATHERWEIGHT", P(TR_DODGE,   EF_PROJ,   MX_AIRY, N, 25, 1, 0, PK_TRAIT, SG_PROJ, 0),
                     P(TR_PASSIVE, EF_STAT,   N, N, 12, 0, 0, PK_TRAIT, SG_DEFENSE, CAP_COST), "-12 max HP, knocked further" },
};

const mx_dodge mx_dodges[DG_COUNT] = {
  { "ROLL",  8, P(TR_DODGE, EF_STAT,  N, N, 0, 0, 3, PK_DODGE, SG_SPEED, 0), "longest i-frames" },
  { "BLINK", 6, P(TR_DODGE, EF_TRAIL, N, N, 0, 3, 2, PK_DODGE, SG_SPEED, 0), "teleport 3 cells; on-dodge effects fire at both ends (x1.5)" },
  { "SLIDE", 4, P(TR_DODGE, EF_AREA,  N, N, 0, 1, 1, PK_DODGE, SG_SPEED, 0), "slide under; knocks down and applies on-dodge effects to all passed" },
};
#undef P
#undef N

uint8_t mx_heal_cap = 8;
const char *const mx_arch_name[ARCH_COUNT] = {
  "FIRE", "FROST", "TIDE", "STORM", "MIRROR", "GROWTH", "ARCANE", "GLITCH", "STONE", "WIND", "HYBRID", "PLAIN"
};

/* how often a trigger fires relative to a plain hit (x16) */
static const uint8_t trig_freq[TR_COUNT] = { 16, 11, 8, 10, 11, 6, 6, 5, 13, 8, 8, 16 };

static void add_piece(const mx_build *b, const mx_piece *p, uint8_t parry, uint8_t push,
                      uint8_t *gm, uint8_t *gn, int16_t *cost, mx_bstats *s)
{
  uint8_t tr = P_TRIG(p), g = P_GROUP(p), ta = P_TAGA(p), tb = P_TAGB(p);
  uint16_t f = trig_freq[tr], m;
  if (P_CAP(p) == CAP_COST) { cost[g] = (int16_t)(cost[g] + P_MAG(p)); return; }
  if (tr == TR_PARRY) f = (uint16_t)(f + f * parry / 2);
  if (tr == TR_PUSH) f = (uint16_t)(f + f * push);
  if (tr == TR_DODGE && b->dodge == DG_BLINK) f = (uint16_t)((f * 3) >> 1);
  if (tr == TR_DODGE && b->dodge == DG_SLIDE) f = (uint16_t)((f * 5) >> 2);
  m = (uint16_t)((P_MAG(p) * f) >> 4);
  if (m > 255) m = 255;
  /* ALIVE pieces in the DEFENSE group are lifesteal: they heal (capped), they do not add armour */
  if (!(g == SG_DEFENSE && ta == MX_ALIVE) && m && gn[g] < 8) gm[g * 8 + gn[g]++] = (uint8_t)m;
  if (ta < MX_TAGS) { s->tags |= TB(ta); s->tagmag[ta] = (uint8_t)(s->tagmag[ta] + m > 255 ? 255 : s->tagmag[ta] + m); }
  if (tb < MX_TAGS) { s->tags |= TB(tb); s->tagmag[tb] = (uint8_t)(s->tagmag[tb] + m / 2 > 255 ? 255 : s->tagmag[tb] + m / 2); }
  if (g == SG_DEFENSE && ta == MX_ALIVE) s->heal = (int16_t)(s->heal + m / 4);
  if (ta == MX_STONE && tr == TR_PUSH) s->push++;
}

void mx_build_stats(const mx_build *b, mx_bstats *s) { mx_build_stats_ex(b, 0, 0, s); }

void mx_build_stats_ex(const mx_build *b, const mx_piece *extra, uint8_t nextra, mx_bstats *s)
{
  uint8_t gm[SG_COUNT * 8], gn[SG_COUNT], i, top = 0;
  int16_t cost[SG_COUNT];
  uint16_t v, tot = 0;
  const mx_wbase *w = &mx_wbases[b->base];
  for (i = 0; i < SG_COUNT; i++) { gn[i] = 0; cost[i] = 0; }
  for (i = 0; i < MX_TAGS; i++) s->tagmag[i] = 0;
  s->tags = 0; s->off = s->def = s->ctrl = s->heal = 0; s->hp = 100;
  for (i = 0; i < MX_TAGS; i++) s->el_mult[i] = 16;
  for (i = 0; i < ST_COUNT; i++) s->st_mult[i] = 16;
  s->mix_pen = 0; s->foe_pct = 0;
  s->aoe = (int16_t)(w->aoe + (b->dodge == DG_SLIDE)); s->pierce = w->pierce; s->push = w->push; s->parry = w->parry; s->proj = w->proj;
  s->avoid = (int16_t)(w->reach + mx_dodges[b->dodge].avoid); s->armorbreak = 0;
  if (b->trait[0] == 5 || b->trait[1] == 5) s->parry = (int16_t)(s->parry + 2); /* SHINY TEETH */
  add_piece(b, &w->p, (uint8_t)s->parry, (uint8_t)s->push, gm, gn, cost, s);
  for (i = 0; i < 2; i++) add_piece(b, &mx_mods[b->mod[i]].p, (uint8_t)s->parry, (uint8_t)s->push, gm, gn, cost, s);
  for (i = 0; i < 2; i++) {
    if (b->trait[i] >= N_TRAITS) continue; /* empty slot (bred heirs may express 0-1 combat traits) */
    add_piece(b, &mx_traits[b->trait[i]].up, (uint8_t)s->parry, (uint8_t)s->push, gm, gn, cost, s);
    add_piece(b, &mx_traits[b->trait[i]].cost, (uint8_t)s->parry, (uint8_t)s->push, gm, gn, cost, s);
  }
  for (i = 0; i < nextra; i++) add_piece(b, &extra[i], (uint8_t)s->parry, (uint8_t)s->push, gm, gn, cost, s);
  if (s->tags & TB(MX_STONE)) s->armorbreak = 1;
  for (i = 0; i < SG_COUNT; i++) {
    v = mx_stack(&gm[i * 8], gn[i], i);
    s->grp[i] = v;
    switch (i) {
    case SG_DAMAGE: s->off = (int16_t)(s->off + v); break;
    case SG_STATUS: s->off = (int16_t)(s->off + (v * 3) / 4); s->ctrl = (int16_t)(s->ctrl + v / 8); break;
    case SG_AREA: s->off = (int16_t)(s->off + v / 2); s->aoe = (int16_t)(s->aoe + v / 25); s->ctrl = (int16_t)(s->ctrl + v / 6); break;
    case SG_PROJ: s->off = (int16_t)(s->off + v / 2); s->avoid = (int16_t)(s->avoid + v / 8); if (v) s->proj++; break;
    case SG_DEFENSE: s->def = (int16_t)(s->def + v); break;
    case SG_SPEED: s->avoid = (int16_t)(s->avoid + v); break;
    case SG_SUMMON: s->off = (int16_t)(s->off + v / 3); s->def = (int16_t)(s->def + v / 4); break;
    default: break;
    }
  }
  s->off = (int16_t)(s->off - cost[SG_DAMAGE]);
  s->def = (int16_t)(s->def - cost[SG_DEFENSE]);
  s->avoid = (int16_t)(s->avoid - cost[SG_SPEED]);
  if (s->heal > mx_heal_cap) s->heal = mx_heal_cap; /* lifesteal per room is capped like any stack */
  s->heal = (int16_t)(s->heal - (s->heal * cost[SG_SUMMON]) / 100);
  s->grp[SG_REACT] = (uint16_t)cost[SG_SUMMON]; /* healing cut % parked here for the sim */
  /* archetype: dominant tag share */
  for (i = 0; i < MX_TAGS; i++) {
    tot = (uint16_t)(tot + s->tagmag[i]);
    if (s->tagmag[i] > s->tagmag[top]) top = i;
  }
  /* archetype = the tag the build carries most; ARCH_HYBRID is kept only as a mono/hybrid flag */
  if (!tot) s->arch = ARCH_PLAIN;
  else s->arch = top;
  s->mono = (uint8_t)(tot && (uint16_t)s->tagmag[top] * 100 >= tot * 55u);
}

/* ---------- abstract combat ---------- */
static const uint8_t body_hp[BD_COUNT] = { 20, 14, 16, 18, 16, 18, 40, 14, 30, 24, 22 };
static const uint8_t atk_dps[AT_COUNT] = { 3, 2, 4, 4, 2, 2, 3, 2 };

static int16_t rx_value(uint8_t rx)
{
  const mx_reaction *x = &mx_rx[rx];
  int16_t v;
  if (x->kind == RK_NULL) return -10;
  if (rx == RX_NONE) return 0;
  v = (int16_t)((x->mult - 16) * 2 + x->ctrl / 3);
  if (x->kind == RK_ARMOR) v = (int16_t)(v + 8);
  if (x->kind == RK_SUMMON || x->kind == RK_HEAL) v = (int16_t)(v + 6);
  if (x->kind == RK_REVEAL || x->kind == RK_LIGHT) v = (int16_t)(v + 4);
  return v;
}

/* reaction bonus for a build against a set of outside tags (enemy elements + room) */
static int16_t react_bonus(const mx_bstats *s, uint16_t outside, uint8_t enemy_proj, uint16_t seed,
                           uint8_t *heal_rx, int16_t *penalty)
{
  uint8_t mags[8], n = 0, a, b, rx, ex, k, j;
  uint8_t seen[RX_COUNT];
  int16_t v;
  uint32_t vv;
  for (k = 0; k < RX_COUNT; k++) seen[k] = 0;
  *heal_rx = 0; *penalty = 0;
  for (a = 0; a < MX_TAGS; a++) {
    if (!(s->tags & TB(a))) continue;
    for (b = 0; b < MX_TAGS; b++) {
      uint16_t inside = (uint16_t)((s->tags & TB(b)) && (b != a || s->tagmag[a] >= 40));
      if (!inside && !(outside & TB(b))) continue;
      rx = mx_react(a, b, (uint8_t)(enemy_proj || s->proj), (uint16_t)(seed + a * 11 + b), &ex);
      for (j = 0; j < 2; j++) {
        uint8_t r = j ? ex : rx;
        if (r == RX_NONE || seen[r]) continue;
        seen[r] = 1;
        v = rx_value(r);
        if (v < 0) { *penalty = (int16_t)(*penalty - v); continue; }
        /* scale by how much of tag a the build actually carries */
        vv = (uint32_t)v * (s->tagmag[a] > 32 ? 32 : s->tagmag[a]) / 32;
        if (mx_rx[r].kind == RK_HEAL) *heal_rx = 1;
        if (n < 8) mags[n++] = (uint8_t)(vv > 255 ? 255 : vv);
        else { /* keep the 8 biggest */
          uint8_t lo = 0;
          for (k = 1; k < 8; k++) if (mags[k] < mags[lo]) lo = k;
          if (vv > mags[lo]) mags[lo] = (uint8_t)(vv > 255 ? 255 : vv);
        }
      }
    }
  }
  return (int16_t)mx_stack(mags, n, SG_REACT);
}

static uint8_t kill_mult(const mx_bstats *s, uint8_t def)
{
  switch (def) {
  case DF_ARMOR: return (uint8_t)((s->pierce || s->armorbreak) ? 16 : 20);
  case DF_WETSHOCK: return (uint8_t)(((s->tags & TB(MX_WET)) && (s->tags & TB(MX_SHOCK))) ? 16 : 24);
  case DF_CRUSH: return (uint8_t)(s->push ? 16 : 24);
  case DF_SPLITS: return (uint8_t)(s->aoe >= 2 ? 18 : 24);
  case DF_REFORM: return (uint8_t)((s->tags & TB(MX_COLD)) ? 16 : 26);
  case DF_REFLECT: return (uint8_t)((s->parry >= 2 || (s->tags & TB(MX_SHINY))) ? 16 : 32);
  case DF_DARK: return (uint8_t)((s->tags & (TB(MX_HOT) | TB(MX_SHINY))) ? 16 : 22);
  default: return 16;
  }
}

uint8_t mx_sim_run(const mx_build *b, const mx_bstats *s, const mx_run *run, uint32_t seed, uint16_t K, uint8_t *depth_out)
{
  mx_rng r;
  int32_t hp, maxhp, pool, edps, t10, dmg, P;
  int16_t off_bonus = 0, react, pen, ctrl, avoid, def, nmob;
  uint8_t i, k, heal_rx, depth;
  uint16_t outside;
  const mx_wbase *w = &mx_wbases[b->base];
  mx_seed(&r, seed);
  maxhp = s->hp; hp = maxhp;
  *depth_out = 0;
  for (i = 0; i < run->nstages; i++) {
    const mx_stage *st = &run->st[i];
    depth = st->floor;
    if (st->type == ST_REST) {
      maxhp = (int32_t)s->hp + depth * 6;
      hp += (35 * (100 - (int32_t)s->grp[SG_REACT])) / 100 + depth * 3;
      if (hp > maxhp) hp = maxhp;
      continue;
    }
    if (st->type == ST_RIFT && hp * 100 < maxhp * 70) continue; /* optional: skip when hurt */
    outside = (uint16_t)((st->have & M_TAGMASK) | st->pickup);
    nmob = 0; pool = 0; edps = 0;
    {
      uint8_t eproj = 0;
      for (k = 0; k < st->nmobs; k++) {
        const mx_mob *m = &st->mob[k];
        int32_t h = body_hp[m->body], d = atk_dps[m->atk], c = st->count[k];
        uint8_t a;
        if (m->el < MX_TAGS) outside |= TB(m->el);
        if (m->atk == AT_PROJ || m->atk == AT_BEAM) eproj = 1;
        h = h * kill_mult(s, m->def) / 16;
        for (a = 0; a < 2; a++) {
          uint8_t af = m->af[a];
          if (af == AF_NONE) continue;
          switch (af) {
          case AF_SHIELD: h = h * 20 / 16; break;
          case AF_VAMP: h = h * 19 / 16; break;
          case AF_SPLIT: h = h * 22 / 16; break;
          case AF_HASTE: d = d * 21 / 16; break;
          case AF_TPHIT: h = h * 18 / 16; break;
          case AF_COLLAPSE: d = d * 18 / 16; break;
          default: outside |= TB(af - AF_AURA0); d = d * 18 / 16; break;
          }
        }
        if (m->el < MX_TAGS) d = d * s->el_mult[m->el] / 16;
        pool += h * c; edps += d * c;
        nmob = (int16_t)(nmob + (m->body == BD_SWARM ? 4 : c));
      }
      if (st->phases) {
        int32_t bh = st->type == ST_BOSS ? 180 : 100, km = 0;
        for (k = 0; k < st->phases; k++) km += kill_mult(s, st->boss.weak[k]);
        pool += bh * km / (16 * st->phases);
        {
          int32_t bd = st->type == ST_BOSS ? 6 : 5;
          uint8_t e;
          for (e = 0; e < 2; e++) {
            uint8_t el = st->boss.el[e];
            bd = bd * (16 + s->el_mult[el]) / 32; /* half weight: a boss is only partly its elements */
          }
          edps += bd;
        }
        outside |= TB(st->boss.el[0]) | TB(st->boss.el[1]);
        for (k = 0; k < st->phases; k++) if (st->boss.atk[k] == AT_PROJ || st->boss.atk[k] == AT_BEAM) eproj = 1;
      }
      react = react_bonus(s, outside, eproj, (uint16_t)(seed + i), &heal_rx, &pen);
    }
    /* scale enemies with depth and heir generation; player power grows with run pickups */
    pool = pool * (16 + depth * 2 + run->gen) / 16;
    edps = edps * (16 + depth + run->gen / 2) / 16;
    edps = edps * s->st_mult[st->type] / 16 * (100 + s->foe_pct) / 100;
    P = (int32_t)w->dps * (100 + s->off + off_bonus + (react * 2) / 3 - pen) * (16 + depth * 2) / 16;
    if (P < 100) P = 100;
    t10 = pool * 1000 / P; /* tenths of a second */
    if (nmob > 1) {
      int16_t aoe = s->aoe > 4 ? 4 : s->aoe, extra = (int16_t)(nmob - 1 > 4 ? 4 : nmob - 1);
      t10 = t10 * 100 / (100 + aoe * extra * 6);
    }
    ctrl = (int16_t)(s->ctrl + react / 3); if (ctrl > 60) ctrl = 60; if (ctrl < 0) ctrl = 0;
    avoid = (int16_t)(40 + s->avoid + (int16_t)mx_below(&r, 31) - 15);
    def = s->def;
    switch (st->type) {
    case ST_HOLDOUT: if (t10 < 300 + depth * 30) t10 = 300 + depth * 30; break;
    case ST_ESCORT: edps = edps * 5 / 4; break;
    case ST_CHASE: t10 = t10 / 2; avoid = (int16_t)(avoid + 10); break;
    case ST_HEIST: edps = edps / 2; break;
    case ST_DARK: if (!(s->tags & (TB(MX_HOT) | TB(MX_SHINY)))) avoid = (int16_t)(avoid - 10); break;
    case ST_MIX: t10 += (st->steps + s->mix_pen) * 50; break;
    case ST_PUZZLE: hp -= st->steps; break;
    default: break;
    }
    if (avoid < 5) avoid = 5;
    if (avoid > 85) avoid = 85;
    if (def > 60) def = 60;
    if (def < -40) def = -40;
    dmg = edps * t10 / 10;
    dmg = dmg * (100 - ctrl) / 100;
    dmg = dmg * (100 - avoid) / 100;
    dmg = dmg * (100 - def) / 100;
    dmg = dmg * K / 256;
    hp -= dmg;
    if (hp <= 0) { *depth_out = depth; return 0; }
    hp += s->heal + (heal_rx ? 4 : 0);
    if (hp > maxhp) hp = maxhp;
    if (st->type == ST_RIFT) off_bonus = (int16_t)(off_bonus + 6);
  }
  *depth_out = RUN_FLOORS;
  return 1;
}
