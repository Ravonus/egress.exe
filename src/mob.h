#ifndef MOB_H
#define MOB_H
/* B2 mobs and combat on the cartridge (docs/redesign.md 6, 7.1-7.3, 7.6, 5.2, 5.5). The API other branches build on;
 * read docs/b2-combat-api.md first. Everything here is banked (src/mob.c, src/mob_ai.c, src/combat.c, src/fx.c);
 * the data a mob needs every frame lives in WRAM (mob[], en[]).
 *
 * A matrix mob is an en[k] (play.h enemy_t: position, state, timers, hp) with en[k].kind == SPAWN_MOB, plus its
 * record mob[k] below (the axes, the family and look, statuses, elite affixes). The B1 kinds (SPAWN_MITE, SPAWN_EYE,
 * SPAWN_BOSS) keep working unchanged; boss.c's summons may stay mites or call mob_spawn. */
#include "play.h"
#include "core/eg_piece.h"

#define SPAWN_MOB 3u

/* ---- 7.1 the axes: the 7-byte mx_mob of proto/matrix/mx.h, same order and values, cart-prefixed names ---- */
enum { MB_WALKER = 0, MB_FLYER, MB_PHASER, MB_BURROWER, MB_CRAWLER, MB_GRAVITY, MB_HEAVY, MB_TELEPORT, MB_SWARM,
       MB_TURRET, MB_MIMIC, MB_COUNT };
enum { MA_LUNGE = 0, MA_PROJ, MA_SLAM, MA_BEAM, MA_TRAIL, MA_SUMMON, MA_GRAB, MA_TERRA, MA_COUNT };
enum { MD_PLAIN = 0, MD_ARMOR, MD_WETSHOCK, MD_CRUSH, MD_SPLITS, MD_REFORM, MD_REFLECT, MD_DARK, MD_COUNT };
enum { MH_COWARD = 0, MH_AGGRO, MH_FLANK, MH_GUARD, MH_LEADER, MH_THIEF, MH_COUNT };
enum { MF_SHIELD = 0, MF_VAMP, MF_SPLIT, MF_HASTE, MF_TPHIT, MF_COLLAPSE, MF_AURA0 /* MF_AURA0 + TAG_* */ };
#define MF_NONE 255u
#define MOB_NOTAG EG_TAGS /* el: a TAG_* of eg_piece.h (the cartridge's 8 tags), or none */
typedef struct {
  uint8_t body, atk, def, el, beh, af0, af1;
} mob_axes_t;

/* ---- the office families (7.1 names, 7.6 bodies). Each has baked looks (tools/bake_mobs.py) ---- */
enum {
  FAM_INTERN = 0, /* clerk blob, walker, lunge: the chaser (sprint away) */
  FAM_CABINET,    /* filing cabinet (L), heavy, slam: crush only - lure it under a falling CRT or into a pit */
  FAM_LAMP,       /* desk lamp on a mount, turret, projectile: only its own orb, returned, hurts it (parry) */
  FAM_FAX,        /* fax thing, hover, kiter, projectile: shots to parry */
  FAM_STAPLES,    /* small clerk blobs, swarm of three, lunge: the charged spin */
  FAM_MEMO,       /* clerk blob behind a clipboard, walker, armoured front: dodge round, lunge into its back */
  FAM_MOLE,       /* cable thing, burrower under foam, grab: surfaces with a tell (dodge) */
  FAM_COUNT
};

/* statuses (5.2 reactions on mobs) */
#define MS_WET 0x01u
#define MS_SHOCK 0x02u
#define MS_COLD 0x04u   /* slowed; a second COLD freezes */
#define MS_FROZEN 0x08u /* solid: shatters on the next heavy hit */
#define MS_BURN 0x10u   /* damage over time */
#define MS_STUN 0x20u
#define MS_UNDER 0x40u  /* burrowed (no hurt box, not drawn but its trail) */
/* flags */
#define MOBF_ELITE 0x01u
#define MOBF_BACK 0x02u   /* the last hit came from behind (armoured fronts) */
#define MOBF_MEANS 0x04u  /* its kill condition's means were checked or placed (7.3) */

typedef struct {
  mob_axes_t ax;
  uint8_t fam, look, slot; /* family, baked look id, loaded look slot (tiles 16 + 24 * slot, palette by group) */
  uint8_t status, st_t;    /* MS_* and the status timer (frames) */
  uint8_t shield;          /* MF_SHIELD hits left */
  uint8_t flags;           /* MOBF_* */
  uint8_t hp_max;
  uint8_t drawn;           /* its body was placed this frame (the overlay follows it) */
  uint8_t speed, tell;     /* walk speed (subpixels a frame), tell frames (>= 12; hasted x0.75) */
  uint8_t tick;            /* burn and status ticks */
} mob_t;
extern uint8_t wmods;      /* the lead's element mods (5.3), found on elites: WM_* */
#define WM_EMBER 0x01u     /* HOT */
#define WM_RIME 0x02u      /* COLD */
#define WM_DAMP 0x04u      /* WET */
extern uint8_t orb_shooter; /* fire_orb_at records it in the orb (orb_t.pad = shooter + 1; 0 = B1) */
/* harness hooks: nonzero dbg_mob[i] = FAM_* + 1 for spawn i (dbg_mob_n spawns at dbg_mob_cell[i]); dbg_elite = the
 * spawn index + 1 made elite, dbg_affix the affixes (af0 | af1 << 4, 0xff none) */
extern uint8_t dbg_mob[6], dbg_mob_cell[6], dbg_mob_n, dbg_elite, dbg_affix;
extern mob_t mob[MAXE];

/* ---- spawning (b2-stages) ---- */
/* After the room is built (floor_build -> actors_spawn): rolls the floor's roster from (floor_seed, floor_tier,
 * floor_no), turns room.spawn[] into mobs, makes at most one an elite (tier >= 1), and places each conditional mob's
 * means (7.3) or rerolls its defence when the room can't hold them. */
void mobs_floor(void) BANKED;
/* One mob now, at a cell centre: `fam` FAM_*, `elite` 0/1, `seed` its variant. Uses a look already loaded this
 * floor when one of that family is (else the family's look is loaded into a free slot: display off only, or
 * during a stage transition; see mobs_preload). Returns the en[] index, or 255 when en[] is full or the cell isn't
 * open floor. Spawn spacing is the caller's (mob_spawn_ok). */
uint8_t mob_spawn(uint8_t fam, uint8_t cell, uint8_t elite, uint16_t seed) BANKED;
/* spacing for spawns mid-floor: open floor, >= 3 cells from the player, >= 2 cells from any live mob, and at most 2
 * live mobs on that cell row (the 10-per-line budget). */
uint8_t mob_spawn_ok(uint8_t cell) BANKED;
/* preload looks for families a stage will spawn later (bitmask of 1 << FAM_*); display off. At most 3 looks. */
void mobs_preload(uint8_t fams) BANKED;
/* live matrix mobs (b2-stages: wave clear) */
uint8_t mobs_alive(void) BANKED;

/* ---- combat (b2-boss builds on it) ---- */
/* where the next enemy_hit came from: player.c sets hit_src before strike(), the actors set it for orbs and crushes */
enum {
  HS_HIT1 = 0, HS_HIT2, HS_HIT3, /* A taps: the combo */
  HS_CHARGED, HS_SPIN, /* A held: the charged strike (level 1), the spin (2, 3) */
  HS_LUNGE,  /* B then A */
  HS_PARRY,  /* A then B: the perfect parry's riposte */
  HS_DODGE, HS_SPRINT, /* B tap, B held: rolling or running through a small (swarm) mob tramples it */
  HS_ORB,    /* a returned (friendly) orb; hit_orb_from = the en[] index that fired it */
  HS_CRUSH,  /* a falling CRT */
  HS_VOID,   /* knocked into a pit */
  HS_FUSE,   /* MOD_FUSE's shock after a dodge */
  HS_REACT,  /* a reaction (burn tick, short circuit) */
  HS_COUNT
};
extern uint8_t hit_src, hit_orb_from;
extern uint8_t hit_tags; /* TG(TAG_*) the hit carries: the weapon's mods (5.3) */
/* results of the last enemy_hit on a matrix mob (harness, HUD) */
#define HR_MISS 0u
#define HR_BLOCK 1u  /* armour, shield, or the kill condition refused it */
#define HR_HURT 2u
#define HR_KILL 3u
extern uint8_t hit_result;
/* kill statistics for the harness: kills by source and by condition */
extern uint8_t kill_by[HS_COUNT];
extern uint8_t cond_kills; /* bit per MD_* defence killed by its condition */

/* hitstop and knockback (6): enemy_hit sets freeze (2..6 frames by damage) and the mob's knockback velocity;
 * heavies take a third, turrets none. A mob knocked into a pit dies (HS_VOID); a falling CRT kills (HS_CRUSH). */

/* ---- 5.2 / 5.5 reactions, particles, sound ---- */
/* the reaction a hit with `tags` makes on a mob of element `el` with statuses `status` (RX_* of eg_piece.h) */
uint8_t combat_react(uint8_t tags, uint8_t el, uint8_t status) BANKED;
/* the particle pool: 8, oldest recycled first; drawn as OP_FX (dropped first on a full line) */
void fx_burst(uint8_t x, uint8_t y, uint8_t kind, uint8_t n) BANKED; /* FX_* below, n particles */
enum { FX_HIT = 0, FX_KILL, FX_HOT, FX_COLD, FX_WET, FX_SHOCK, FX_SHINY, FX_ALIVE, FX_MAGIC, FX_GLITCH, FX_STONE,
       FX_ELITE, FX_COUNT };

/* ---- the look pipeline (b2-obstacles) ---- */
/* Baked by tools/bake_mobs.py from tools/mock/mobgen.py into src/gen/mob_art.c: per look, frames of the body (palette
 * A, colour 1..3) and an overlay (palette B: ink + one cue colour). Sprite tiles 16..87 (freed by the heir) are 3
 * look slots of 24 tiles, 88..111 the particle and marker shapes; OBJ palettes 2 and 3 are the floor's two body ramps (a look slot uses 2 + (group)), OBJ 5
 * the overlay/FX palette (1 ink, 2 group 0's cue, 3 group 1's cue), OBJ 7 the elite and tell palette. */
#define MOB_TILE0 16u
#define MOB_SLOT_TILES 24u
#define MOB_SLOTS 3u
#define MOB_FX_TILE 88u /* 88..111: the FX and marker tiles (src/gen/mob_art.h FXT_*), 2 per shape */
uint8_t mob_look_load(uint8_t slot, uint8_t look, uint8_t group) BANKED; /* display off; returns frames */
void mob_group_pal(uint8_t group, uint8_t ramp, uint8_t el) BANKED;       /* OBJ palette 2 + group, cue into 5 */
/* draw a loaded look: frame f (0 idle, 1 idle/move, 2 tell, 3 dissolve if baked) at the sprite box top-left
 * (sx, sy) screen coordinates; returns 0 if the per-line budget refused it this frame (it flickers). */
uint8_t mob_look_draw(uint8_t slot, uint8_t f, int16_t sx, int16_t sy, uint8_t prop_or) BANKED;

/* ---- hooks called from the B1 modules (small edits there) ---- */
void mobs_update(void) BANKED;      /* from enemies_update: every SPAWN_MOB */
void mobs_draw(uint8_t pass) BANKED; /* from draw_sprites: OP_MARK (elite crowns), OP_OVER (overlays) */
void mob_hit(uint8_t k, uint8_t dmg, uint8_t fromx, uint8_t fromy) BANKED; /* from enemy_hit for SPAWN_MOB */
void mob_died(uint8_t k) BANKED;    /* from enemy_kill: splits, tokens, fx */
void mobs_reload(void) BANKED;      /* from sprites_reset (display off): the floor's looks back into VRAM */
void mob_draw(uint8_t k) BANKED;    /* from draw_sprites' OP_MOB loop: one mob's body */
void fx_draw(void) BANKED;          /* from draw_sprites, OP_FX: moves and draws the particle pool */
void fx_reset(void) BANKED;

#endif
