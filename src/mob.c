/* B2 mobs on the cartridge (docs/redesign.md 6, 7.1-7.3, 7.6; the API is src/mob.h, docs/b2-combat-api.md).
 *
 * One file on purpose: banked const tables are only readable from their own bank, and the roster, the AI and the
 * damage rules all read the family table. Per-mob numbers the AI needs every frame are copied into mob[] at spawn.
 *
 * The office families, each with a job from 6 (enemies are designed so all six moves matter):
 *   INTERN   walker, aggressive, lunge after an 18-frame tell                      sprint away, dodge the lunge
 *   STAPLES  swarm of three small ones, quick lunges, 1 hp                         the charged spin; trample them
 *   MEMO     walker with a clipboard: an armoured front, it turns to face you      dodge round it, lunge its back
 *   CABINET  heavy, slow, a slam ring after a 28-frame tell; crush only            lure it under a falling CRT,
 *                                                                                  push one into it, or knock it
 *                                                                                  into a pit
 *   LAMP     turret, fires an orb after a 16-frame tell; only its own orb hurts it parry (or lash) it back
 *   FAX      flyer, keeps its distance, fires orbs                                 parry; chase it down
 *   MOLE     burrower: travels under foam and carpet (a bump), surfaces with a tell and grabs; it digs back down
 *            through fragile floor (a pit where it dove)                            dodge the grab, hit it while up
 * Mob tags against fragile floor (eg_room breakable-by masks): walkers BB_MOB_WALKER (step off: weight 1), the
 * cabinet BB_MOB_HEAVY (weight 2), the mole BB_MOB_BURROWER (breaks the cell it dives through). Protected cells are
 * never worn by mobs. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "mob.h"
#include "oam.h"
#include "heir.h"
#include "puzzle.h"
#include "stage.h"
#include "gen/mob_art.h"
#include "core/eg_input.h"

extern eg_input_t pin;
extern volatile uint8_t video_epoch;
extern uint8_t frame_epoch;

mob_t mob[MAXE];
uint8_t hit_src, hit_orb_from, hit_tags, hit_result, cond_kills, wmods, orb_shooter = 255u;
uint8_t kill_by[HS_COUNT];
uint8_t dbg_mob[6], dbg_mob_cell[6], dbg_mob_n, dbg_elite, dbg_affix = 0xffu;
uint8_t mob_slot_look[MOB_SLOTS]; /* harness: the floor's looks */
uint8_t mob_slot_bmask[MOB_SLOTS];
static uint8_t slot_tag[MOB_SLOTS], slot_fam[MOB_SLOTS], grp_set;
static uint16_t elite_pal[3];
uint8_t mob_has_elite;
static uint8_t seen_name, roll_recent;

/* ---- the families (7.1 axes; proto/matrix/mx_mob.c accepts each one) ---- */
typedef struct {
  uint8_t body, atk, def, beh, hp, speed, tell, touch;
} fam_t;
static const fam_t FAM[FAM_COUNT] = {
    {MB_WALKER, MA_LUNGE, MD_PLAIN, MH_AGGRO, 3u, 12u, 18u, 1u},   /* INTERN */
    {MB_HEAVY, MA_SLAM, MD_CRUSH, MH_GUARD, 6u, 6u, 28u, 1u},      /* CABINET */
    {MB_TURRET, MA_PROJ, MD_REFLECT, MH_GUARD, 2u, 0u, 16u, 1u},   /* LAMP */
    {MB_FLYER, MA_PROJ, MD_PLAIN, MH_COWARD, 2u, 8u, 16u, 1u},     /* FAX */
    {MB_SWARM, MA_LUNGE, MD_PLAIN, MH_AGGRO, 1u, 16u, 14u, 1u},    /* STAPLES */
    {MB_WALKER, MA_LUNGE, MD_ARMOR, MH_FLANK, 3u, 10u, 18u, 1u},   /* MEMO */
    {MB_BURROWER, MA_GRAB, MD_PLAIN, MH_FLANK, 2u, 10u, 18u, 1u},  /* MOLE */
};
/* what each kind of body may stand on (by cell type): W walkers, K knocked back (W + pits), F flyers, U burrowed */
#define CW 1u
#define CK 2u
#define CF 4u
#define CU 8u
static const uint8_t cell_ok[T_TYPES] = {
    CW | CK | CF | CU, /* EMPTY */
    CF | CU,           /* DIRT */
    0,                 /* WALL */
    CF,                /* BRICK */
    CF,                /* ROCK */
    CF,                /* TOKEN */
    CF,                /* KEY */
    0,                 /* DOOR */
    CW | CK | CF | CU, /* CRUMBLE */
    CK | CF,           /* PIT */
    CW | CK | CF | CU, /* PLATE */
    CF,                /* GATE */
    CW | CK | CF,      /* GATE_OPEN */
    CF,                /* WATER */
    CF,                /* GLITCH */
    CF,                /* PAPER */
    CW | CK | CF,      /* PLANK */
    CF,                /* ITEM */
    CF,                /* SOCKET */
    CF,                /* SOCKET_ON */
    CF,                /* DEAD */
    CF,                /* BLOCK */
    CF,                /* BLOCK_PLATE */
    CW | CK | CF | CU, CW | CK | CF | CU, CW | CK | CF | CU, /* WORN */
    CW | CK | CF | CU, CW | CK | CF | CU,                    /* CEIL */
    CW | CK | CF,                                            /* GLASS */
    CW | CK | CF | CU, CW | CK | CF | CU,                    /* ICE */
};

static uint8_t ok_at(uint8_t x, uint8_t y, uint8_t bit) { return (uint8_t)(cell_ok[cell_type(cell_at(x, y))] & bit); }
/* a 10x10 box around (x, y) all on cells `bit` allows */
static uint8_t box_ok(uint8_t x, uint8_t y, uint8_t bit) {
  uint8_t l,r,t,b;
  if((uint8_t)(x-5u)>246u || (uint8_t)(y-5u)>214u) return 0u;
  l=(uint8_t)((x-5u)>>4);r=(uint8_t)((x+4u)>>4);
  t=(uint8_t)((y-5u)&0xf0u);b=(uint8_t)((y+4u)&0xf0u);
  if(!(cell_ok[cell_type(l|t)]&bit)) return 0u;
  if(l!=r && !(cell_ok[cell_type(r|t)]&bit)) return 0u;
  if(t!=b && (!(cell_ok[cell_type(l|b)]&bit) || (l!=r && !(cell_ok[cell_type(r|b)]&bit)))) return 0u;
  return 1u;
}
static uint8_t bit_of(uint8_t body) {
  return body == MB_FLYER ? CF : (body == MB_BURROWER ? CU : CW);
}

/* ---- looks and palettes ---- */
static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return (uint16_t)((r >> 3) | ((uint16_t)(g >> 3) << 5) | ((uint16_t)(b >> 3) << 10)); }

void mob_group_pal(uint8_t group, uint8_t look, uint8_t el) BANKED {
  mob_look_t L;
  uint16_t *p = &pal_sp[(uint8_t)((2u + group) << 2)];
  (void)el;
  mob_art_info(look, &L);
  p[1] = L.pal[0];
  p[2] = L.pal[1];
  p[3] = L.pal[2];
  pal_sp[21] = rgb(20, 14, 46); /* OBJ 5: ink, then each group's cue */
  pal_sp[(uint8_t)(22u + group)] = L.cue;
  pal_dirty = 1u;
}

uint8_t mob_look_load(uint8_t slot, uint8_t look, uint8_t group) BANKED {
  mob_look_t L;
  if (look < MOB_ART_SPLIT) mob_art_upload0(look, (uint8_t)(MOB_TILE0 + slot * MOB_SLOT_TILES), group);
  else mob_art_upload1(look, (uint8_t)(MOB_TILE0 + slot * MOB_SLOT_TILES), group);
  mob_art_info(look, &L);
  mob_slot_bmask[slot] = L.bmask;
  return 4u;
}

/* display off: every slot's look back into VRAM, the FX shapes, the palettes */
void mobs_reload(void) BANKED {
  uint8_t s;
  mob_art_fx(MOB_FX_TILE);
  for (s = 0; s != MOB_SLOTS; s++)
    if (mob_slot_look[s] != 255u) mob_look_load(s, mob_slot_look[s], (uint8_t)(s & 1u));
  for (s = 0; s != 2u; s++)
    if (mob_slot_look[s] != 255u) mob_group_pal(s, mob_slot_look[s], 0);
  if (mob_has_elite) {
    pal_sp[29] = elite_pal[0];
    pal_sp[30] = elite_pal[1];
    pal_sp[31] = elite_pal[2];
  }
}

/* pick a look of family `fam` for slot `s` (group s & 1): slot 2 shares group 0's palette, so it takes a variant of
 * that group's tag or an untagged one; returns the look id */
static uint8_t pick_look(uint8_t fam, uint8_t s, uint8_t r) {
  mob_look_t L;
  uint8_t v, i, look;
  v = (uint8_t)(r % MOB_LOOKS_PER_FAM);
  if (s < 2u) return (uint8_t)(fam * MOB_LOOKS_PER_FAM + v);
  for (i = 0; i != MOB_LOOKS_PER_FAM; i++) {
    look = (uint8_t)(fam * MOB_LOOKS_PER_FAM + (uint8_t)((v + i) % MOB_LOOKS_PER_FAM));
    mob_art_info(look, &L);
    if (L.tag == slot_tag[0] || L.tag == EG_TAGS) return look;
  }
  return (uint8_t)(fam * MOB_LOOKS_PER_FAM);
}

static uint8_t slot_for(uint8_t fam) {
  uint8_t s;
  mob_look_t L;
  for (s = 0; s != MOB_SLOTS; s++)
    if (mob_slot_look[s] != 255u && slot_fam[s] == fam) return s;
  for (s = 0; s != MOB_SLOTS; s++)
    if (mob_slot_look[s] == 255u) {
      mob_slot_look[s] = pick_look(fam, s, eg_rnd());
      slot_fam[s] = fam;
      mob_art_info(mob_slot_look[s], &L);
      slot_tag[s] = L.tag;
      mob_slot_bmask[s] = L.bmask;
      return s;
    }
  return 255u;
}

void mobs_preload(uint8_t fams) BANKED {
  uint8_t f;
  for (f = 0; f != FAM_COUNT; f++)
    if (fams & (uint8_t)(1u << f)) slot_for(f);
  mobs_reload();
}

/* ---- spawning ---- */
static uint8_t affix_ok(mob_t *m, uint8_t a) { /* proto/matrix/mx_mob.c mx_affix_ok, for the office axes */
  uint8_t b = m->ax.body;
  switch (a) {
    case MF_SHIELD: return (uint8_t)(m->ax.def != MD_WETSHOCK);
    case MF_VAMP: return (uint8_t)(m->ax.def != MD_REFORM && b != MB_SWARM);
    case MF_SPLIT: return (uint8_t)(b != MB_SWARM && b != MB_TURRET && m->ax.def != MD_SPLITS);
    case MF_HASTE: return (uint8_t)(b != MB_TURRET && ((m->tell * 3u) >> 2) >= 12u);
    case MF_TPHIT: return (uint8_t)(m->ax.def != MD_ARMOR && b != MB_TURRET && b != MB_MIMIC);
    case MF_COLLAPSE: return (uint8_t)(b == MB_WALKER || b == MB_HEAVY);
    default: return (uint8_t)(a < MF_AURA0 + EG_TAGS && (uint8_t)(a - MF_AURA0) != m->ax.el);
  }
}

static uint8_t free_en(void) {
  uint8_t k;
  for (k = 0; k != MAXE; k++)
    if (k >= nen || en[k].st == ES_DEAD) return k;
  return 255u;
}

static uint8_t spawn_at(uint8_t k, uint8_t fam, uint8_t cell, uint8_t elite) {
  enemy_t *e = &en[k];
  mob_t *m = &mob[k];
  const fam_t *f = &FAM[fam];
  uint8_t s = slot_for(fam), i, a;
  if (s == 255u) return 255u;
  memset(e, 0, sizeof *e);
  memset(m, 0, sizeof *m);
  e->kind = SPAWN_MOB;
  e->x = cell_sub(EG_X(cell));
  e->y = cell_sub(EG_Y(cell));
  e->tx = (uint8_t)(e->x >> 4);
  e->ty = (uint8_t)(e->y >> 4);
  e->anim = (uint8_t)(k * 53u);
  e->st = f->body == MB_FLYER ? ES_FLOAT : ES_REST;
  e->t = (uint8_t)(20u + k * 9u);
  e->cd = (uint8_t)(60u + k * 17u);
  e->face = F_DOWN;
  m->fam = fam;
  m->slot = s;
  m->look = mob_slot_look[s];
  m->ax.body = f->body;
  m->ax.atk = f->atk;
  m->ax.def = f->def;
  m->ax.beh = f->beh;
  m->ax.el = slot_tag[s];
  m->ax.af0 = m->ax.af1 = MF_NONE;
  m->speed = f->speed;
  m->tell = f->tell;
  e->hp = (uint8_t)(f->hp + (fam != FAM_STAPLES && fam != FAM_LAMP ? (floor_tier >> 1) : 0u));
  if (f->body == MB_BURROWER) m->status = MS_UNDER;
  if (elite) {
    m->flags = MOBF_ELITE;
    if (dbg_affix != 0xffu) {
      m->ax.af0 = (uint8_t)(dbg_affix & 15u);
      m->ax.af1 = (uint8_t)(dbg_affix >> 4) == 15u ? MF_NONE : (uint8_t)(dbg_affix >> 4);
    } else
      for (i = 0; i != 2u; i++) {
        if (i && (eg_rnd() & 1u)) break; /* one or two */
        for (a = 0; a != 8u; a++) {
          uint8_t c = eg_rndn(MF_AURA0 + 2u);
          if (c >= MF_AURA0) c = (uint8_t)(MF_AURA0 + (m->ax.el != TAG_HOT ? TAG_HOT : TAG_COLD) + (c & 1u) * 0u);
          if (!affix_ok(m, c) || c == m->ax.af0) continue;
          if (i && m->ax.af0 >= MF_AURA0 && c >= MF_AURA0) continue; /* one aura */
          if (i && ((m->ax.af0 == MF_VAMP && c == MF_SHIELD) || (m->ax.af0 == MF_SHIELD && c == MF_VAMP) ||
                    (m->ax.af0 == MF_HASTE && c == MF_TPHIT) || (m->ax.af0 == MF_TPHIT && c == MF_HASTE) ||
                    (m->ax.af0 == MF_SPLIT && c == MF_VAMP) || (m->ax.af0 == MF_VAMP && c == MF_SPLIT)))
            continue;
          if (i) m->ax.af1 = c;
          else m->ax.af0 = c;
          break;
        }
      }
    e->hp = (uint8_t)(e->hp * 2u);
    if (m->ax.af0 == MF_SHIELD || m->ax.af1 == MF_SHIELD) m->shield = 3u;
    if (m->ax.af0 == MF_HASTE || m->ax.af1 == MF_HASTE) {
      m->tell = (uint8_t)((m->tell * 3u) >> 2);
      m->speed = (uint8_t)(m->speed + (m->speed >> 1));
    }
    mob_has_elite = 1u;
    elite_pal[0] = rgb(20, 14, 46);
    elite_pal[1] = mob_art_affix_col(m->ax.af0);
    elite_pal[2] = m->ax.af1 != MF_NONE ? mob_art_affix_col(m->ax.af1) : rgb(255, 246, 200);
  }
  m->hp_max = e->hp;
  if (k >= nen) nen = (uint8_t)(k + 1u);
  return k;
}

uint8_t mob_spawn(uint8_t fam, uint8_t cell, uint8_t elite, uint16_t seed) BANKED {
  uint8_t k = free_en();
  (void)seed;
  if (k == 255u || fam >= FAM_COUNT || !(cell_ok[cell_type(cell)] & (CW | CF))) return 255u;
  if (spawn_at(k, fam, cell, elite) == 255u) return 255u;
  enemies_alive++;
  if (mob_has_elite) {
    pal_sp[29] = elite_pal[0];
    pal_sp[30] = elite_pal[1];
    pal_sp[31] = elite_pal[2];
    pal_dirty = 1u;
  }
  return k;
}

uint8_t mob_spawn_ok(uint8_t cell) BANKED {
  uint8_t k, row = 0, cx = EG_X(cell), cy = EG_Y(cell), pc = cell_at(pl_x, pl_y);
  if (!(cell_ok[cell_type(cell)] & CW)) return 0;
  if (absdiff(cx, EG_X(pc)) + absdiff(cy, EG_Y(pc)) < 3u) return 0;
  for (k = 0; k != nen; k++) {
    uint8_t c;
    if (en[k].st == ES_DEAD) continue;
    c = cell_at((uint8_t)(en[k].x >> 4), (uint8_t)(en[k].y >> 4));
    if (absdiff(cx, EG_X(c)) + absdiff(cy, EG_Y(c)) < 2u) return 0;
    if (EG_Y(c) == cy && ++row >= 2u) return 0;
  }
  return 1u;
}

uint8_t mobs_alive(void) BANKED {
  uint8_t k, n = 0;
  for (k = 0; k != nen; k++)
    if (en[k].kind == SPAWN_MOB && en[k].st != ES_DEAD) n++;
  return n;
}

/* the floor's roster from the room's spawns */
static uint8_t room_has(uint8_t type) {
  uint8_t i = 0;
  do {
    if (cell_type(i) == type) return 1u;
  } while (++i != (uint8_t)EG_N);
  return 0;
}

void mobs_floor(void) BANKED {
  uint8_t k, n, fam, ground[2], ranged, ng, cell, elite_k = 255u, r;
  memset(mob, 0, sizeof mob);
  memset(mob_slot_look, 255u, sizeof mob_slot_look);
  grp_set = 0;
  mob_has_elite = 0;
  seen_name = 0;
  fx_reset();
  if (floor_no <= 1u) wmods = 0;
  memset(kill_by, 0, sizeof kill_by);
  cond_kills = 0;
  nen = 0;
  enemies_alive = 0;
  if (dbg_mob_n) { /* the harness's arena */
    for (k = 0; k != dbg_mob_n && k != 6u; k++)
      if (spawn_at(k, (uint8_t)(dbg_mob[k] - 1u), dbg_mob_cell[k], (uint8_t)(dbg_elite == k + 1u)) != 255u)
        enemies_alive++;
    return;
  }
  if (dbg_no_enemies) return;
  /* the roster: the office brings its families in over the first floors (6: every move matters) */
  r = eg_rnd();
  if (stage_type != SG_PUZZLE || floor_no <= 1u) {
    ground[0] = FAM_INTERN;
    ng = 1u;
    ranged = FAM_FAX;
  } else if (floor_no == 2u) {
    ground[0] = FAM_INTERN;
    ground[1] = FAM_STAPLES;
    ng = 2u;
    ranged = FAM_FAX;
  } else {
    static const uint8_t pool[5] = {FAM_INTERN, FAM_STAPLES, FAM_MEMO, FAM_CABINET, FAM_MOLE};
    ground[0] = pool[r % 5u];
    ground[1] = pool[(uint8_t)(r / 5u + 1u + ground[0]) % 5u];
    if (ground[1] == ground[0]) ground[1] = ground[0] == FAM_INTERN ? FAM_MEMO : FAM_INTERN;
    ng = 2u;
    ranged = (r & 0x80u) ? FAM_LAMP : FAM_FAX;
  }
  /* fair by construction (7.3): a crush-only cabinet needs a CRT or a pit in the room; a mole needs foam to cross */
  for (k = 0; k != ng; k++) {
    if (ground[k] == FAM_CABINET && !room_has(T_ROCK) && !room_has(T_PIT)) ground[k] = FAM_MEMO;
    if (ground[k] == FAM_MOLE && !room_has(T_DIRT)) ground[k] = FAM_INTERN;
  }
  if (floor_no >= 3u && room.nspawn) elite_k = (uint8_t)(r % room.nspawn);
  n = 0;
  for (k = 0; k != room.nspawn; k++) {
    cell = room.spawn[k];
    fam = room.spawn_kind[k] == SPAWN_EYE ? ranged : ground[k % ng];
    if (spawn_at(n, fam, cell, (uint8_t)(k == elite_k)) == 255u) continue;
    n++;
    enemies_alive++;
    if (fam == FAM_STAPLES) { /* a swarm: two more on free neighbouring carpet */
      uint8_t j, c2;
      for (j = 0; j != 4u && n < MAXE - 1u; j++) {
        c2 = (uint8_t)(cell + (int8_t)(fdy[j] * 16 + fdx[j]));
        if (cell_type(c2) != T_EMPTY) continue;
        if (spawn_at(n, FAM_STAPLES, c2, 0) == 255u) break;
        en[n].x = (uint16_t)(en[n].x + (j & 1u ? 64u : (uint16_t)-64));
        n++;
        enemies_alive++;
        if ((j & 1u) && n >= 3u) break;
      }
    }
    if (n >= MAXE - 1u) break;
  }
}

/* ---- the AI ---- */
static enemy_t *E;
static mob_t *Mb;
static uint8_t K, ex, ey;

static void tell(uint8_t frames) {
  E->st = ES_TELL;
  E->t = frames;
  sfx(SFX_TELL);
}

static void approach(uint16_t *v, uint8_t target, uint8_t speed) {
  uint16_t t = (uint16_t)target << 4;
  if (*v < t) *v = (uint16_t)(*v + speed) > t ? t : (uint16_t)(*v + speed);
  else if (*v > t) *v = (uint16_t)(*v - speed) < t ? t : (uint16_t)(*v - speed);
}

/* a mob left `cell` for another: fragile floor under walkers and heavies wears (never a protected cell) */
static void stepped_off(uint8_t cell) {
  uint8_t t = cell_type(cell), who, j;
  if (!EG_FRAGILE(t) || EG_PROT(&room, cell)) return;
  who = Mb->ax.body == MB_HEAVY ? BB_MOB_HEAVY : BB_MOB_WALKER;
  j = eg_frag_kind_of(t);
  if (eg_step_off(&room, cell, who, Mb->ax.body == MB_HEAVY ? 2u : 1u)) {
    puzzle_break_fx(cell, j);
    puzzle_dirty = 1u;
  } else
    queue_cell(cell);
}

/* collapse trail (elite affix): the carpet it leaves rots (never protected cells, never next to the player) */
static void trail(uint8_t cell) {
  if (cell_type(cell) == T_EMPTY && !EG_PROT(&room, cell) && (eg_rnd() & 3u) == 0u &&
      cell != cell_at(pl_x, pl_y)) {
    room.c[cell] = T_CRUMBLE;
    queue_cell(cell);
  }
}

/* the next cell to walk to: toward the player (or beside them, for flankers), never into another mob's target */
static void pick(uint8_t bit) {
  static const int8_t neighbour[4]={-16,16,-1,1};
  /* Steering is retried at the current cell centre next tick. Position,
   * collision, attack tells and statuses still update every simulation tick. */
  if(video_epoch!=frame_epoch || (LY_REG>=56u && LY_REG<144u))return;
  uint8_t c = cell_at(ex, ey), d, best = 255u, bd = 255u, n, k, j, gx = pl_x, gy = pl_y, nx, ny, dist;
  uint8_t rotation=eg_rnd(),wobble=eg_rnd();
  enemy_t *other;
  if (Mb->status & MS_BLIND) { gx=(uint8_t)(eg_rnd()&0xf0u); gy=(uint8_t)(eg_rnd()&0xb0u); }
  if (Mb->ax.beh == MH_FLANK) { /* aim beside the player, on the side it already is */
    if (absdiff(ex, pl_x) < absdiff(ey, pl_y)) gx = (uint8_t)(pl_x + (K & 1u ? 24u : (uint8_t)-24));
    else gy = (uint8_t)(pl_y + (K & 1u ? 24u : (uint8_t)-24));
  }
  for (k = 0; k != 4u; k++) {
    /* Visit each neighbour once in a seeded order. Re-rolling every probe
     * both repeated directions and spent up to eight RNG calls per choice. */
    d = (uint8_t)((k + rotation) & 3u);
    n = (uint8_t)(c + neighbour[d]);
    if (!(cell_ok[cell_type(n)] & bit)) continue;
    nx = (uint8_t)(EG_X(n) * 16u + 8u);
    ny = (uint8_t)(EG_Y(n) * 16u + 8u);
    other=en;
    for (j = 0; j != nen; j++,other++)
      if (j != K && other->st != ES_DEAD && other->tx == nx && other->ty == ny) break;
    if (j != nen) continue;
    dist = (uint8_t)(absdiff(nx, gx) + absdiff(ny, gy));
    if ((wobble & 7u) == k) dist = (uint8_t)(dist >> 1);
    if (dist < bd) {
      bd = dist;
      best = d;
    }
  }
  if (best == 255u) {
    E->tx = (uint8_t)(EG_X(c) * 16u + 8u);
    E->ty = (uint8_t)(EG_Y(c) * 16u + 8u);
    return;
  }
  n = (uint8_t)(c + neighbour[best]);
  E->tx = (uint8_t)(EG_X(n) * 16u + 8u);
  E->ty = (uint8_t)(EG_Y(n) * 16u + 8u);
  if (Mb->ax.def == MD_ARMOR) /* the clipboard turns to the player */
    E->face = absdiff(ex, pl_x) > absdiff(ey, pl_y) ? (ex < pl_x ? F_RIGHT : F_LEFT) : (ey < pl_y ? F_DOWN : F_UP);
  else
    E->face = best;
}

/* a straight, open line between two cells in the same row or column */
static uint8_t clear_line(uint8_t a, uint8_t b, uint8_t step) {
  while (a != b) {
    a = (uint8_t)(a + step);
    if (!(cell_ok[cell_type(a)] & CW)) return 0;
  }
  return 1u;
}

/* at a cell centre: lined up with the player, close, nothing between: face and tell */
static uint8_t try_lunge(uint8_t range) {
  uint8_t c = cell_at(ex, ey), p = cell_at(pl_x, pl_y);
  if (E->cd) return 0;
  if (EG_Y(c) == EG_Y(p) && absdiff(ex, pl_x) < range && clear_line(c, p, ex < pl_x ? 1u : 255u)) {
    E->face = ex < pl_x ? F_RIGHT : F_LEFT;
  } else if (EG_X(c) == EG_X(p) && absdiff(ey, pl_y) < range && clear_line(c, p, ey < pl_y ? 16u : 240u)) {
    E->face = ey < pl_y ? F_DOWN : F_UP;
  } else
    return 0;
  tell(Mb->tell);
  return 1u;
}

static void walker(void) {
  uint8_t nx, ny, bit = bit_of(Mb->ax.body);
  switch (E->st) {
    case ES_WALK: {
      uint8_t c0 = cell_at(ex, ey);
      approach(&E->x, E->tx, Mb->speed);
      approach(&E->y, E->ty, Mb->speed);
      nx = (uint8_t)(E->x >> 4);
      ny = (uint8_t)(E->y >> 4);
      if (cell_at(nx, ny) != c0) {
        stepped_off(c0);
        if (Mb->ax.af0 == MF_COLLAPSE || Mb->ax.af1 == MF_COLLAPSE) trail(c0);
      }
      if (nx == E->tx && ny == E->ty) {
        ex = nx;
        ey = ny;
        if (Mb->ax.atk == MA_SLAM) {
          if (!E->cd && absdiff(ex, pl_x) + absdiff(ey, pl_y) < 34u) tell(Mb->tell);
          else pick(bit);
        } else if (!try_lunge(Mb->fam == FAM_STAPLES ? 48u : 72u))
          pick(bit);
      }
      if (E->cd) E->cd--;
      break;
    }
    case ES_TELL:
      if (!--E->t) {
        if (Mb->ax.atk == MA_SLAM) { /* the ring */
          shake = 6u;
          sfx(SFX_THUD);
          fx_burst(ex, ey, FX_STONE, 4u);
          if (absdiff(ex, pl_x) < 28u && absdiff(ey, pl_y) < 28u) player_hurt(2u, ex, ey);
          E->st = ES_REST;
          E->t = 50u;
          E->cd = 60u;
        } else {
          E->st = ES_LUNGE;
          E->t = Mb->fam == FAM_STAPLES ? 10u : 16u;
          E->vx = (int8_t)(fdx[E->face] * (Mb->fam == FAM_STAPLES ? 48 : 44));
          E->vy = (int8_t)(fdy[E->face] * (Mb->fam == FAM_STAPLES ? 48 : 44));
        }
      }
      break;
    case ES_LUNGE:
      nx = (uint8_t)((uint16_t)(E->x + E->vx) >> 4);
      ny = (uint8_t)((uint16_t)(E->y + E->vy) >> 4);
      if (!box_ok(nx, ny, bit) || !--E->t) {
        E->st = ES_REST;
        E->t = Mb->fam == FAM_STAPLES ? 24u : 36u;
        E->cd = 40u;
      } else {
        E->x = (uint16_t)(E->x + E->vx);
        E->y = (uint16_t)(E->y + E->vy);
      }
      break;
    case ES_REST:
      if (!--E->t) {
        uint8_t c = cell_at(ex, ey);
        E->st = ES_WALK;
        E->tx = (uint8_t)(EG_X(c) * 16u + 8u);
        E->ty = (uint8_t)(EG_Y(c) * 16u + 8u);
      }
      break;
  }
}

static void shoot(void) {
  orb_shooter = K;
  fire_orb_at(ex, (uint8_t)(ey - 6u), (int16_t)pl_x - ex, (int16_t)pl_y - (int16_t)(ey - 6u), 20u);
  orb_shooter = 255u;
  sfx(SFX_SHOT);
}

static void flyer(void) {
  uint8_t d;
  int8_t mx = 0, my = 0;
  uint16_t nx, ny;
  switch (E->st) {
    case ES_REST:
    case ES_FLOAT:
      E->st = ES_FLOAT;
      d = (uint8_t)(absdiff(ex, pl_x) + absdiff(ey, pl_y));
      if (d > 72u) {
        mx = ex < pl_x ? 6 : -6;
        my = ey < pl_y ? 6 : -6;
      } else if (d < 46u) {
        mx = ex < pl_x ? -8 : 8;
        my = ey < pl_y ? -8 : 8;
      } else {
        mx = (E->anim & 128u) ? 5 : -5;
        my = (E->anim & 128u) ? -5 : 5;
        if (absdiff(ex, pl_x) > absdiff(ey, pl_y)) mx = 0;
        else my = 0;
      }
      E->face = ex < pl_x ? F_RIGHT : F_LEFT;
      if (E->cd) E->cd--;
      else if (d < 110u)
        tell(Mb->tell);
      break;
    case ES_TELL:
      if (!--E->t) {
        shoot();
        E->st = ES_FLOAT;
        E->cd = (uint8_t)(100u + eg_rndn(60u));
      }
      break;
  }
  if (mx) {
    nx = (uint16_t)(E->x + mx);
    if (ok_at((uint8_t)(nx >> 4), ey, CF) && box_ok((uint8_t)(nx >> 4), ey, CF)) E->x = nx;
  }
  if (my) {
    ny = (uint16_t)(E->y + my);
    if (box_ok((uint8_t)(E->x >> 4), (uint8_t)(ny >> 4), CF)) E->y = ny;
  }
}

static void turret(void) {
  E->face = ex < pl_x ? F_RIGHT : F_LEFT;
  if (E->st == ES_TELL) {
    if (!--E->t) {
      shoot();
      E->st = ES_REST;
      E->cd = (uint8_t)(110u + eg_rndn(40u));
    }
    return;
  }
  E->st = ES_REST;
  if (E->cd) E->cd--;
  else if (absdiff(ex, pl_x) + absdiff(ey, pl_y) < 120u)
    tell(Mb->tell);
}

static void burrower(void) {
  uint8_t c;
  if (Mb->status & MS_UNDER) {
    approach(&E->x, E->tx, Mb->speed);
    approach(&E->y, E->ty, Mb->speed);
    if ((uint8_t)(E->x >> 4) == E->tx && (uint8_t)(E->y >> 4) == E->ty) {
      ex = E->tx;
      ey = E->ty;
      c = cell_at(ex, ey);
      if (E->cd) E->cd--;
      if (!E->cd && cell_ok[cell_type(c)] & CW && absdiff(ex, pl_x) + absdiff(ey, pl_y) < 40u) {
        Mb->status &= (uint8_t)~MS_UNDER; /* up: the tell */
        fx_burst(ex, ey, FX_STONE, 2u);
        E->face = absdiff(ex, pl_x) > absdiff(ey, pl_y) ? (ex < pl_x ? F_RIGHT : F_LEFT) : (ey < pl_y ? F_DOWN : F_UP);
        tell(Mb->tell);
      } else
        pick(CU);
    }
    if (E->cd > 20u) E->cd = 20u;
    return;
  }
  switch (E->st) {
    case ES_TELL:
      if (!--E->t) {
        E->st = ES_LUNGE;
        E->t = 10u;
        E->vx = (int8_t)(fdx[E->face] * 40);
        E->vy = (int8_t)(fdy[E->face] * 40);
      }
      break;
    case ES_LUNGE: {
      uint8_t nx = (uint8_t)((uint16_t)(E->x + E->vx) >> 4), ny = (uint8_t)((uint16_t)(E->y + E->vy) >> 4);
      if (!box_ok(nx, ny, CW) || !--E->t) {
        E->st = ES_REST;
        E->t = 60u; /* open */
      } else {
        E->x = (uint16_t)(E->x + E->vx);
        E->y = (uint16_t)(E->y + E->vy);
      }
      break;
    }
    case ES_WALK:
    case ES_REST:
    default:
      if (E->st == ES_WALK) E->t = 1u;
      if (!--E->t) { /* back down: through fragile floor it leaves a pit */
        c = cell_at(ex, ey);
        E->x = cell_sub(EG_X(c));
        E->y = cell_sub(EG_Y(c));
        E->tx = (uint8_t)(E->x >> 4);
        E->ty = (uint8_t)(E->y >> 4);
        if (EG_FRAGILE(cell_type(c))) puzzle_frag_hit(c, BB_MOB_BURROWER);
        Mb->status |= MS_UNDER;
        E->st = ES_WALK;
        E->cd = 60u;
        fx_burst(ex, ey, FX_STONE, 2u);
      }
      break;
  }
}

/* knocked back: slides, slowing; into a pit it falls (the void) */
static void knocked(void) {
  uint8_t nx = (uint8_t)((uint16_t)(E->x + E->vx) >> 4), ny = (uint8_t)((uint16_t)(E->y + E->vy) >> 4), c0;
  uint8_t bit = Mb->ax.body == MB_FLYER ? CF : CK;
  c0 = cell_at(ex, ey);
  if (box_ok(nx, ny, bit) || (bit == CK && ok_at(nx, ny, CK))) {
    E->x = (uint16_t)(E->x + E->vx);
    E->y = (uint16_t)(E->y + E->vy);
    if (cell_at(nx, ny) != c0 && Mb->ax.body != MB_FLYER) stepped_off(c0);
  }
  E->vx = (int8_t)(E->vx / 2 + E->vx / 4);
  E->vy = (int8_t)(E->vy / 2 + E->vy / 4);
  if (!--E->t) {
    uint8_t c = cell_at((uint8_t)(E->x >> 4), (uint8_t)(E->y >> 4));
    E->st = Mb->ax.body == MB_FLYER ? ES_FLOAT : ES_REST;
    E->t = 12u;
    if (E->cd < 30u) E->cd = 30u;
    E->tx = (uint8_t)(EG_X(c) * 16u + 8u);
    E->ty = (uint8_t)(EG_Y(c) * 16u + 8u);
  }
}

static void status_tick(void) {
  if (!Mb->st_t) return;
  if (!--Mb->st_t) {
    if (Mb->status & MS_FROZEN) fx_burst(ex, ey, FX_COLD, 2u);
    Mb->status &= MS_UNDER;
    return;
  }
  if ((Mb->status & MS_BURN) && (Mb->st_t & 31u) == 0u) {
    hit_src = HS_REACT;
    hit_tags = 0;
    fx_burst(ex, (uint8_t)(ey - 8u), FX_HOT, 1u);
    enemy_hit(K, 1u, ex, ey);
  }
}

void mobs_update(void) BANKED {
  uint8_t c, t;
  if (pl_state == PS_ROLL) roll_recent = 45u;
  else if (roll_recent) roll_recent--;
  for (K = 0; K != nen; K++) {
    E = &en[K];
    if (E->kind != SPAWN_MOB || E->st == ES_DEAD) continue;
    Mb = &mob[K];
    ex = (uint8_t)(E->x >> 4);
    ey = (uint8_t)(E->y >> 4);
    E->anim++;
    if (E->flash) E->flash--;
    status_tick();
    if (E->st == ES_DEAD) continue;
    /* crushed (a CRT fell or was pushed into its cell) or fallen (a pit under it) */
    c = cell_at(ex, ey);
    t = cell_type(c);
    if (t == T_ROCK && !(Mb->status & MS_UNDER)) {
      hit_src = HS_CRUSH;
      enemy_kill(K, 1u);
      continue;
    }
    if ((t == T_PIT || t == T_WATER) && Mb->ax.body != MB_FLYER && !(Mb->status & MS_UNDER)) {
      hit_src = HS_VOID;
      fx_burst(ex, ey, t == T_WATER ? FX_WET : FX_KILL, 3u);
      enemy_kill(K, 1u);
      continue;
    }
    if (Mb->status & (MS_FROZEN | MS_STUN)) continue;
    if (E->st == ES_HURT) {
      knocked();
      continue;
    }
    if ((Mb->status & MS_COLD) && (E->anim & 1u)) continue; /* slowed: half the frames */
    if (Mb->status & MS_BLIND) {
      if (!(E->anim&15u)) { pick(Mb->ax.body==MB_FLYER ? CF : CW); fx_burst(ex,ey,FX_STONE,1u); }
      if (Mb->ax.body!=MB_TURRET) { approach(&E->x,E->tx,Mb->speed); approach(&E->y,E->ty,Mb->speed); }
      continue;
    }
    switch (Mb->ax.body) {
      case MB_FLYER: flyer(); break;
      case MB_TURRET: turret(); break;
      case MB_BURROWER: burrower(); break;
      default: walker(); break;
    }
    /* touch and lunge damage (a tell, the burrowed and the frozen don't hurt) */
    if (E->st != ES_TELL && !(Mb->status & MS_UNDER) && overlap(ex, ey, pl_x, pl_y, 11u, 11u)) {
      uint8_t taken = acts.hits_taken;
      player_hurt(E->st == ES_LUNGE && Mb->fam != FAM_STAPLES ? 2u : 1u, ex, ey);
      if (acts.hits_taken != taken && (Mb->ax.af0 == MF_VAMP || Mb->ax.af1 == MF_VAMP) && E->hp < Mb->hp_max)
        E->hp++;
    }
    /* trampled: a roll or a run through a small one (6) */
    if (Mb->fam == FAM_STAPLES && (pl_state == PS_ROLL) && overlap(ex, ey, pl_x, pl_y, 10u, 10u) && E->st != ES_HURT) {
      hit_src = pin.move == EG_MV_SPRINT ? HS_SPRINT : HS_DODGE;
      hit_tags = 0;
      enemy_hit(K, 1u, pl_x, pl_y);
    }
  }
}

/* ---- damage (6, 7.2, 7.3, 5.2) ---- */
static uint8_t weapon_tags(void) {
  uint8_t t = hit_tags;
  if (hit_src <= HS_SPRINT) {
    if (wmods & WM_EMBER) t |= TG(TAG_HOT);
    if (wmods & WM_RIME) t |= TG(TAG_COLD);
    if (wmods & WM_DAMP) t |= TG(TAG_WET);
  }
  return t;
}

uint8_t combat_react(uint8_t tags, uint8_t el, uint8_t status) BANKED {
  return eg_combat_reaction(tags,el,status,(uint8_t)(hit_src==HS_FUSE || (hit_src==HS_SPIN && pl_level>=3u)));
}

uint8_t mob_tool(uint8_t k, uint8_t piece) BANKED {
  mob_t *m=&mob[k]; enemy_t *e=&en[k];
  uint8_t x=(uint8_t)(e->x>>4),y=(uint8_t)(e->y>>4);
  if(e->kind!=SPAWN_MOB || e->st==ES_DEAD || (m->status&MS_UNDER)) return 0u;
  if(piece==P_AIR) {
    if(m->ax.body==MB_HEAVY || m->ax.body==MB_TURRET) return 0u;
    e->vx=x>pl_x ? 56 : x<pl_x ? -56 : 0;
    e->vy=y>pl_y ? 56 : y<pl_y ? -56 : 0;
    e->st=ES_HURT;e->t=10u;fx_burst(x,y,FX_KILL,3u);return 1u;
  }
  if(piece==P_WATER || piece==P_RAIN || piece==P_CLOUD) {
    m->status=(uint8_t)((m->status&~MS_BURN)|MS_WET);m->st_t=240u;
    fx_burst(x,y,FX_WET,3u);return 1u;
  }
  if(piece==P_MUD || piece==P_EARTH) {
    m->status|=MS_COLD;m->st_t=180u;fx_burst(x,y,FX_STONE,3u);return 1u;
  }
  if(piece==P_STEAM || piece==P_DUST) {
    m->status|=MS_BLIND;m->st_t=150u;e->st=ES_WALK;
    e->tx=x;e->ty=y;fx_burst(x,y,FX_KILL,4u);return 1u;
  }
  if(piece==P_ELECTRIC && m->ax.el==TAG_STONE && !(m->status&MS_WET)) return 0u;
  hit_src=piece==P_ELECTRIC ? HS_FUSE : HS_HIT1;
  hit_tags=eg_piece_tags(piece);hit_result=HR_MISS;
  enemy_hit(k,piece==P_LAVA ? 2u : 1u,pl_x,pl_y);
  if(hit_result==HR_MISS || hit_result==HR_BLOCK) return 0u;
  if(piece==P_ELECTRIC && e->st!=ES_DEAD && !(m->status&MS_STUN)) { m->status|=MS_STUN;m->st_t=30u; }
  return 1u;
}

/* the hit came from the side it faces? */
static uint8_t from_front(uint8_t fromx, uint8_t fromy) {
  uint8_t d = absdiff(fromx, ex) > absdiff(fromy, ey) ? (fromx < ex ? F_LEFT : F_RIGHT) : (fromy < ey ? F_UP : F_DOWN);
  return (uint8_t)(d == E->face);
}

static void say_name(void) {
  mob_look_t L;
  if (seen_name & (uint8_t)(1u << Mb->slot)) return;
  seen_name |= (uint8_t)(1u << Mb->slot);
  mob_art_info(Mb->look, &L);
  hud_msg(L.name);
}

static void block(void) {
  hit_result = HR_BLOCK;
  E->flash = 4u;
  freeze = 2u;
  sfx(SFX_DENY);
  fx_burst(ex, (uint8_t)(ey - 8u), FX_HIT, 1u);
  say_name();
}

void mob_hit(uint8_t k, uint8_t dmg, uint8_t fromx, uint8_t fromy) BANKED {
  uint8_t rx, tags, ok = 1u, kb = 40u;
  K = k;
  E = &en[k];
  Mb = &mob[k];
  ex = (uint8_t)(E->x >> 4);
  ey = (uint8_t)(E->y >> 4);
  hit_result = HR_MISS;
  if (Mb->status & MS_UNDER) return; /* nothing to hit: it's under the floor */
  /* the kill condition (7.3) */
  switch (Mb->ax.def) {
    case MD_CRUSH: ok = (uint8_t)(hit_src == HS_CRUSH || hit_src == HS_VOID || hit_src == HS_REACT); break;
    case MD_REFLECT: ok = (uint8_t)(hit_src == HS_ORB && hit_orb_from == k); break;
    case MD_ARMOR: ok = (uint8_t)(hit_src >= HS_ORB || !from_front(fromx, fromy)); break;
  }
  if ((Mb->flags & MOBF_HUNT_BACK) && !roll_recent) ok = 0u;
  if (Mb->shield && hit_src != HS_REACT) {
    Mb->shield--;
    fx_burst(ex, (uint8_t)(ey - 8u), FX_SHINY, 2u);
    if (!Mb->shield) hud_msg("SHIELD BREAKS");
    ok = 0;
  }
  tags = weapon_tags();
  rx = ok ? combat_react(tags, Mb->ax.el, Mb->status) : RX_NONE;
  /* reactions on mobs (5.2): statuses and multipliers */
  switch (rx) {
    case RX_NONE: break;
    case RX_FREEZE:
    case RX_DEEPFREEZE:
      Mb->status = (uint8_t)((Mb->status & MS_UNDER) | MS_FROZEN);
      Mb->st_t = 150u;
      fx_burst(ex, ey, FX_COLD, 3u);
      break;
    case RX_SHORT:
      dmg = (uint8_t)(dmg * 2u + 1u);
      Mb->status = (uint8_t)((Mb->status & MS_UNDER) | MS_STUN);
      Mb->st_t = 60u;
      fx_burst(ex, ey, FX_SHOCK, 3u);
      break;
    case RX_BURN:
    case RX_EMBER:
    case RX_FLARE:
      Mb->status |= MS_BURN;
      Mb->st_t = 160u;
      fx_burst(ex, ey, FX_HOT, 2u);
      break;
    case RX_BRITTLE:
    case RX_CRUSH:
    case RX_GLOWHEAT:
      dmg = (uint8_t)(dmg * 2u);
      fx_burst(ex, ey, FX_STONE, 2u);
      break;
    case RX_THAW:
    case RX_STEAM:
      dmg++;
      Mb->status &= (uint8_t)(MS_UNDER | MS_BURN);
      fx_burst(ex, ey, FX_WET, 2u);
      break;
    default:
      dmg++;
      fx_burst(ex, ey, (uint8_t)(FX_HOT + (Mb->ax.el < EG_TAGS ? Mb->ax.el : 0u)), 2u);
      break;
  }
  if (ok && rx == RX_NONE) { /* plain element statuses from the lead's mods */
    if (tags & TG(TAG_WET)) {
      Mb->status |= MS_WET;
      Mb->st_t = 240u;
    }
    if ((tags & TG(TAG_COLD)) && !(Mb->status & MS_FROZEN)) {
      Mb->status |= MS_COLD;
      Mb->st_t = 120u;
    }
  }
  if ((Mb->status & MS_FROZEN) && ok && rx == RX_NONE && (dmg >= 2u || hit_src == HS_SPIN)) {
    dmg = (uint8_t)(dmg * 3u); /* shattered */
    Mb->status &= (uint8_t)~MS_FROZEN;
    fx_burst(ex, ey, FX_COLD, 4u);
  }
  /* knockback: a third for heavies, none for turrets; the spin and the lunge push harder */
  if (hit_src == HS_SPIN || hit_src == HS_CHARGED || hit_src == HS_LUNGE) kb = 56u;
  if (Mb->ax.body == MB_HEAVY) kb = (uint8_t)(kb / 3u + (hit_src == HS_SPIN ? 8u : 0u));
  if (Mb->ax.body == MB_TURRET || (Mb->status & MS_FROZEN)) kb = 0;
  if (kb && hit_src != HS_REACT && hit_src != HS_CRUSH) {
    E->vx = ex > fromx ? (int8_t)kb : (ex < fromx ? (int8_t)-kb : 0);
    E->vy = ey > fromy ? (int8_t)kb : (ey < fromy ? (int8_t)-kb : 0);
    if (absdiff(ex, fromx) > absdiff(ey, fromy)) E->vy = 0;
    else E->vx = 0;
    E->st = ES_HURT;
    E->t = Mb->ax.body == MB_HEAVY ? 8u : 10u;
  }
  if (!ok) {
    block();
    return;
  }
  E->flash = 8u;
  freeze = (uint8_t)(dmg >= 4u ? 6u : (dmg >= 2u ? 4u : 2u)); /* hitstop */
  sfx(SFX_HIT);
  fx_burst(ex, (uint8_t)(ey - 8u), FX_HIT, 1u);
  if (E->hp <= dmg) {
    E->hp = 0;
    enemy_kill(k, 0);
    return;
  }
  E->hp = (uint8_t)(E->hp - dmg);
  hit_result = HR_HURT;
  if ((Mb->ax.af0 == MF_TPHIT || Mb->ax.af1 == MF_TPHIT) && Mb->ax.body != MB_TURRET) { /* blinks away */
    uint8_t i, c;
    for (i = 0; i != 8u; i++) {
      c = (uint8_t)(cell_at(ex, ey) + (int8_t)(fdy[i & 3u] * (i < 4u ? 48 : 32) + fdx[i & 3u] * (i < 4u ? 3 : 2)));
      if (cell_type(c) == T_EMPTY) {
        fx_burst(ex, ey, FX_MAGIC, 2u);
        E->x = cell_sub(EG_X(c));
        E->y = cell_sub(EG_Y(c));
        E->tx = (uint8_t)(E->x >> 4);
        E->ty = (uint8_t)(E->y >> 4);
        E->st = ES_REST;
        E->t = 20u;
        break;
      }
    }
  }
}

void mob_died(uint8_t k) BANKED {
  enemy_t *e = &en[k];
  mob_t *m = &mob[k];
  uint8_t x = (uint8_t)(e->x >> 4), y = (uint8_t)(e->y >> 4), j;
  hit_result = HR_KILL;
  if (hit_src < HS_COUNT && kill_by[hit_src] != 255u) kill_by[hit_src]++;
  if (m->ax.def == MD_CRUSH || m->ax.def == MD_REFLECT || m->ax.def == MD_ARMOR)
    cond_kills |= (uint8_t)(1u << m->ax.def);
  fx_burst(x, (uint8_t)(y - 8u), FX_KILL, 3u);
  if (m->flags & MOBF_ELITE) {
    if (tokens < 250u) tokens = (uint8_t)(tokens + 2u); /* 2x tokens (enemy_kill gave the first) */
    fx_burst(x, (uint8_t)(y - 8u), FX_ELITE, 3u);
    /* a found-only piece: the lead's next element mod */
    j = (uint8_t)(~wmods & 7u);
    if (j) {
      j = (uint8_t)(j & (uint8_t)-j);
      wmods |= j;
      hud_msg(j == WM_EMBER ? "EMBER: THE LEAD BURNS" : (j == WM_RIME ? "RIME: THE LEAD FREEZES" : "DAMP: THE LEAD SOAKS"));
    }
    if ((m->ax.af0 == MF_SPLIT || m->ax.af1 == MF_SPLIT)) { /* splits once */
      m->ax.af0 = m->ax.af1 = MF_NONE;
      for (j = 0; j != 2u; j++) {
        uint8_t c = (uint8_t)(cell_at(x, y) + (j ? 1u : 255u)), kk;
        if (cell_type(c) != T_EMPTY) continue;
        for (kk = 0; kk != MAXE; kk++)
          if (kk != k && (kk >= nen || en[kk].st == ES_DEAD)) break;
        if (kk == MAXE) continue;
        if (spawn_at(kk, m->fam, c, 0) != 255u) {
          en[kk].hp = 1u;
          enemies_alive++;
        }
      }
    }
  }
}
