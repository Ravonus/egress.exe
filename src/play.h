#ifndef PLAY_H
#define PLAY_H
/* What the floor's modules share: the player, the actors, the grid helpers (bank 0, callable from any bank) and the
 * per-run state (mods, factions, the truth matrix). */
#include "egress.h"
#include "gen/art.h"

#define MAXE 8u
#define MAXO 8u
#define MAXP 6u
#define GRAVITY_FRAMES 8u

enum { PS_FREE = 0, PS_LASH, PS_ROLL, PS_SPIN, PS_HURT, PS_EXIT, PS_FALL };
enum { F_UP = 0, F_DOWN, F_LEFT, F_RIGHT };
enum { ES_WALK = 0, ES_TELL, ES_LUNGE, ES_REST, ES_HURT, ES_FLOAT, ES_DEAD, ES_DIVE, ES_STUN };
#define SPAWN_BOSS 2u

typedef struct {
  uint8_t kind, st, t, hp, flash, face, cd, anim;
  uint16_t x, y; /* subpixels (x16) */
  int8_t vx, vy;
  uint8_t tx, ty; /* walk target (pixels); the boss: its attack and phase */
} enemy_t;
typedef struct {
  uint8_t live, friendly, life, pad;
  uint16_t x, y;
  int8_t vx, vy;
} orb_t;
typedef struct {
  uint8_t t, pal, tile, x, y;
} puff_t;

/* the player */
extern uint8_t pl_x, pl_y, pl_hp, pl_hp_max, pl_state, pl_face;
extern uint8_t pl_t, pl_inv, pl_combo, pl_queue, pl_charge, roll_cd, push_t, hit_mask, walk_t, combo_gap;
extern int8_t roll_dx, roll_dy, kb_dx, kb_dy;
extern uint16_t psx, psy;
extern int8_t __at(0xDB9A) pl_z; /* presentation height; the logical position stays on the map */
extern uint8_t __at(0xDB9B) pl_air;
/* the floor */
extern uint8_t tokens, keys_held, door_open, board_busy, cam_x, cam_y, enemies_alive;
extern uint8_t room_origin_x,room_origin_y,room_width,room_height,bg_origin_x,bg_origin_y;
#define BG_SCROLL_X(x) ((uint8_t)((x)+(bg_origin_x<<3)))
#define BG_SCROLL_Y(y) ((uint8_t)((y)+(bg_origin_y<<3)))
extern uint8_t freeze, shake, hud_dirty, grav_t, boss_floor;
extern uint8_t cracked[EG_N / 8u];
void grid_shift_view(int8_t dx,int8_t dy) BANKED;
void player_shift_view(int8_t dx,int8_t dy) BANKED;
extern enemy_t en[MAXE];
extern uint8_t nen;
extern orb_t orbs[MAXO];
extern puff_t puffs[MAXP];
extern uint8_t meta_tile[M_COUNT][4], meta_pal[M_COUNT];
extern uint16_t people[ART_SKINS + ART_HAIRS + ART_HOODIES + ART_TROUSERS];
extern const int8_t fdx[4], fdy[4]; /* main.c: bank 0 */

/* what the player did on this floor (the matrix reads it when the floor ends) */
typedef struct {
  uint8_t mites, eyes, dug, pushes, breaks, picked, deflects, hits_taken;
  uint16_t frames;
} floor_acts_t;
extern floor_acts_t acts;

/* --- per-run state (run.c) --- */
/* weapon mods: one weapon, the lead; mods slot into it */
#define MOD_LONG 0x01u   /* a longer lash */
#define MOD_STATIC 0x02u /* charges faster */
#define MOD_SURGE 0x04u  /* the third lash and the spin hit harder */
#define MOD_GROUND 0x08u /* deflected orbs hit much harder */
#define MOD_FUSE 0x10u   /* a dodge roll ends in a shock */
#define MOD_SPLICE 0x20u /* every third kill mends half a heart */
#define MOD_SLOTS 3u
extern uint8_t mods;     /* installed this run */
extern uint8_t kills_run;
enum { FAC_COMPILE = 0, FAC_CHARGE, FAC_SKY, FAC_WATCH, FAC_ROOT, FAC_LOCK, FAC_COUNT };
extern int8_t standing[FAC_COUNT];
/* the truth matrix: leanings on three questions, three answers each (never rolled; play moves it) */
enum { TR_WHAT = 0, TR_WHERE = 3, TR_WHY = 6 };
extern uint8_t truth[9];
uint8_t truth_leader(uint8_t axis) BANKED; /* 0..2 */

/* --- bank 0 helpers (grid.c) --- */
#define cell_type(i) ((uint8_t)(room.c[(uint8_t)(i)] & T_MASK))
uint8_t cell_at(uint8_t x, uint8_t y) NONBANKED;
uint16_t cell_sub(uint8_t c) NONBANKED;
uint8_t is_cracked(uint8_t i) NONBANKED;
uint8_t box_free(uint8_t x, uint8_t y) NONBANKED;
uint8_t mite_free(uint8_t x, uint8_t y) NONBANKED;
uint8_t eye_free(uint8_t x, uint8_t y) NONBANKED;
uint8_t absdiff(uint8_t a, uint8_t b) NONBANKED;
uint8_t overlap(uint8_t ax, uint8_t ay, uint8_t bx, uint8_t by, uint8_t rx, uint8_t ry) NONBANKED;
/* Attack centres may extend outside the map; they must never wrap to its other side. */
uint8_t overlap_world(int16_t ax, int16_t ay, uint8_t bx, uint8_t by, uint8_t rx, uint8_t ry) BANKED;
void queue_cell(uint8_t i) NONBANKED;
void flush_cells(uint8_t max) BANKED;
void draw_cell(uint8_t i) BANKED;
void cells_reset(void) BANKED;
void puff(uint8_t x, uint8_t y, uint8_t pal, uint8_t tile) BANKED;
uint8_t enemy_in_cell(uint8_t c) NONBANKED;

/* --- player.c --- */
void player_update(void) BANKED;
void player_tool_cast(void) BANKED;
void player_hurt(uint8_t n, uint8_t fromx, uint8_t fromy) BANKED;
void player_fall(uint8_t safe) BANKED;
void open_door(void) BANKED;
/* --- actors.c --- */
void actors_spawn(void) BANKED;
void enemies_update(void) BANKED;
void orbs_update(void) BANKED;
void enemy_hit(uint8_t k, uint8_t dmg, uint8_t fromx, uint8_t fromy) BANKED;
void enemy_kill(uint8_t k, uint8_t crushed) BANKED;
void actors_crush(void) BANKED;
void fire_orb_at(uint8_t ex, uint8_t ey, int16_t dx, int16_t dy, uint8_t speed) BANKED;
/* --- sprites.c --- */
void draw_sprites(void) BANKED;
void sprites_reset(void) BANKED;
/* --- hud.c (banked; hud_msg's bank-0 front copies a string from any bank) --- */
void hud_draw(void) BANKED;
void hud_charge(void) BANKED;
void hud_msg(const char *s) NONBANKED;
void hud_tick(void) BANKED;
void hud_init(void) BANKED; /* a floor's HUD, no toast: the window at 128 */
/* --- boss.c --- */
void boss_build(void) BANKED;
void boss_update(uint8_t k) BANKED;
#define CHARGE_FULL (mods & MOD_STATIC ? 22u : 34u)

#endif
