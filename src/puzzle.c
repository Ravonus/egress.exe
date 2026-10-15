/* B1 puzzle rooms on the cartridge: the pocket, the portable bench (SELECT), using a piece on the cell you face, the
 * reaction as it spreads, and how the reaction tiles are drawn until the look branch's tileset lands.
 *
 * The rules are the shared core's (src/core/eg_piece.c: eg_recipe, eg_apply), so the cartridge does exactly what the
 * host solver planned. Globals the PyBoy harness reads are plain: pocket, pocket_n, bench_on, bench_cur, last_rx. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "puzzle.h"
#include "core/eg_piece.h"
#include "core/eg_plan.h"
#include "run.h"

uint8_t pocket[EG_MAX_ITEM], pocket_n;
uint8_t puzzle_dirty, frag_skip, floor_resets;
eg_plan_state plan;
static uint8_t pl_cell, pl_safe, beat_was, frag_pend;
uint8_t dead_t; /* the dead-room hint: 0 none, counting down, 1 shown (the harness reads it) */
static uint8_t frag_t[4][2]; /* timed resets: cell, ticks of 8 frames left (0: free) */
uint8_t bench_on, bench_cur, last_rx; /* bench_on: 1 the bench is up, 2 a use is playing out */
static uint8_t sock[2], told;
static char buf[20];

void pocket_reset(void) BANKED {
  memset(pocket, 0, sizeof pocket);
  memset(frag_t, 0, sizeof frag_t);
  pl_cell = pl_safe = room.start;
  frag_pend = 255u;
  puzzle_dirty = frag_skip = dead_t = 0;
  beat_was = 2u;
  pocket_n = 0;
  bench_on = 0;
  bench_cur = 0;
  last_rx = 0;
}

static void pocket_take(uint8_t slot) {
  uint8_t k;
  for (k = slot; k + 1u < pocket_n; k++) pocket[k] = pocket[(uint8_t)(k + 1u)];
  pocket_n--;
  pocket[pocket_n] = 0;
}

/* the player walked into a piece lying in the room */
void puzzle_pick(uint8_t cell) BANKED {
  uint8_t p = eg_item_at(&room, cell);
  if (!p || pocket_n >= EG_MAX_ITEM) return;
  pocket[pocket_n++] = p;
  room.c[cell] = T_EMPTY;
  queue_cell(cell);
  sfx(SFX_KEY);
  puff(pl_x, (uint8_t)(pl_y - 10u), 4, ST_SPARK);
  if (acts.picked != 255u) acts.picked++;
  if (!told) {
    told = 1;
    hud_msg("SELECT: BENCH");
  } else {
    eg_piece_name(p, buf);
    hud_msg(buf);
  }
}

/* --- stand-in looks for the cell types from T_CRUMBLE up (the look branch maps the same ids to its own tiles) ---
 * one row per type: the existing metatile drawn, and the palette it is drawn in */
static const uint8_t look[T_TYPES - T_CRUMBLE][2] = {
    {M_CARPET_C, 1},    /* T_CRUMBLE    rotten carpet: carpet in the foam's plum */
    {M_WALL_CAP, 6},    /* T_PIT        a hole, the void: the wall cap in navy */
    {M_TOKEN, 6},       /* T_PLATE      a pressure plate: the token disc in keycard navy and blue */
    {M_BRICK, 4},       /* T_GATE       a shutter: board in the CRT's taupe */
    {M_CARPET_A, 4},    /* T_GATE_OPEN  the shutter up: carpet, cyan-lit */
    {M_FOAM_A, 4},      /* T_WATER      the flood: ripples in the CRT's cyan */
    {M_CARPET_C, 4},    /* T_GLITCH     glitched glass, on the beat: cyan-lit carpet (off the beat: the void) */
    {M_BRICK_CRACK, 5}, /* T_PAPER      paper and posters: cracked board in brass and cream */
    {M_CARPET_B, 5},    /* T_PLANK      a cardboard plank: carpet in brass */
    {0, 0},             /* T_ITEM       drawn as its four letters (below) */
    {M_KEY, 3},         /* T_SOCKET     a dead socket: the card slot in grey */
    {M_KEY, 7},         /* T_SOCKET_ON  live: in EXIT green */
    {M_WALL_VENT, 7},   /* T_DEAD       a dead shutter: the vent grille in dark green */
    {M_BRICK, 1},       /* T_BLOCK      a laid brick: board in brick plum */
    {M_BRICK, 1},       /* T_BLOCK_PLATE */
    {M_CARPET_B, 5},    /* T_WORN       worn carpet tile: brass (crack stages: carpet, carpet, cracked board) */
    {M_CARPET_C, 5},    /* T_WORN_1 */
    {M_BRICK_CRACK, 5}, /* T_WORN_2 */
    {M_BRICK, 3},       /* T_CEIL       a ceiling tile: blue-grey board, then cracked */
    {M_BRICK_CRACK, 3}, /* T_CEIL_1 */
    {M_WALL_CAP, 4},    /* T_GLASS      glass over the void: the void, cyan-tinted */
    {M_CARPET_C, 3},    /* T_ICE        thin ice: pale blue-white carpet, then foam-cracked */
    {M_FOAM_B, 3},      /* T_ICE_1 */
};

/* a piece's palette, by its first trait: HOT plum-pink, WET and COLD blue-chalk, STONE ochre, SHINY cyan, else green */
static uint8_t piece_pal(uint8_t p) {
  uint8_t t = eg_piece_tags(p);
  if (t & TG(TAG_HOT)) return 1u;
  if (t & (TG(TAG_WET) | TG(TAG_COLD))) return 3u;
  if (t & TG(TAG_SHINY)) return 4u;
  if (t & TG(TAG_STONE)) return 0u;
  return 7u;
}

void puzzle_draw(uint8_t i) BANKED {
  uint8_t t = cell_type(i), x = (uint8_t)(EG_X(i) << 1), y = (uint8_t)(EG_Y(i) << 1), m, a[4];
  if (t == T_ITEM) {
    uint8_t p = eg_item_at(&room, i);
    eg_piece_abbr(p, buf);
    m = piece_pal(p);
    buf[4] = buf[2];
    buf[5] = buf[3];
    buf[2] = 0;
    buf[6] = 0;
    text_at(0, x, y, buf, m);
    text_at(0, x, (uint8_t)(y + 1u), buf + 4, m);
    return;
  }
  if (t == T_GLITCH && !eg_beat_on(frame_count)) t = T_PIT; /* off the beat: the void shows */
  if (t == T_PIT) { /* the void: the HUD's night blue */
    text_at(0, x, y, "  ", 4);
    text_at(0, x, (uint8_t)(y + 1u), "  ", 4);
    return;
  }
  if (t == T_GLASS) { /* glass: a glint over the void */
    text_at(0, x, y, "/ ", 4);
    text_at(0, x, (uint8_t)(y + 1u), " /", 4);
    return;
  }
  m = look[t - T_CRUMBLE][0];
  a[0] = a[1] = a[2] = a[3] = (uint8_t)(look[t - T_CRUMBLE][1] | 0x08u);
  VBK_REG = 1;
  set_bkg_tiles(x, y, 2, 2, a);
  VBK_REG = 0;
  set_bkg_tiles(x, y, 2, 2, meta_tile[m]);
}

/* --- using a piece --- */
static uint8_t body(uint8_t *occ) {
  uint8_t n = 0, c, k, a, b, xs[2], ys[2];
  xs[0] = (uint8_t)(pl_x - 5u);
  xs[1] = (uint8_t)(pl_x + 4u);
  ys[0] = (uint8_t)(pl_y - 5u);
  ys[1] = (uint8_t)(pl_y + 4u);
  for (a = 0; a != 2u; a++)
    for (b = 0; b != 2u; b++) {
      c = cell_at(xs[b], ys[a]);
      for (k = 0; k != n; k++)
        if (occ[k] == c) break;
      if (k == n) occ[n++] = c;
    }
  return n;
}

static void wait_frames(uint8_t n) {
  while (n--) {
    frame_wait();
    sound_tick();
    hud_tick();
  }
}

/* use pocket[slot] on the cell the player faces; 1 if the room reacted */
static uint8_t use(uint8_t slot) {
  uint8_t occ[4], n = body(occ), here = cell_at(pl_x, pl_y), cell, rx, k, nd, pal;
  uint8_t order[EG_DIRTY_MAX];
  cell = (uint8_t)(here + (uint8_t)(fdy[pl_face] * (int8_t)EG_W) + (uint8_t)fdx[pl_face]);
  if (slot >= pocket_n || enemy_in_cell(cell)) return 0;
  rx = eg_apply(&room, pocket[slot], cell, occ, n);
  if (!rx) {
    sfx(SFX_DENY);
    hud_msg("NOTHING HAPPENS");
    return 0;
  }
  last_rx = rx;
  pocket_take(slot);
  nd = eg_ndirty;
  if (nd <= EG_DIRTY_MAX) memcpy(order, eg_dirty, nd);
  eg_rx_name(rx, buf);
  hud_msg(buf);
  sfx(rx == RX_BURN ? SFX_BREAK : (rx == RX_SHORT ? SFX_DOOR : (rx == RX_WEIGHT ? SFX_THUD : SFX_CHARGED)));
  /* the spread, a cell at a time, nearest first */
  pal = rx == RX_BURN ? 7u : (rx == RX_SHORT ? 4u : (rx == RX_WEIGHT ? 4u : 4u));
  if (nd > EG_DIRTY_MAX) {
    for (k = 0; k != (uint8_t)EG_N; k++) draw_cell(k);
  } else
    for (k = 0; k != nd; k++) {
      cell = order[k];
      draw_cell(cell);
      puff((uint8_t)(EG_X(cell) * 16u + 8u), (uint8_t)(EG_Y(cell) * 16u + 8u), pal,
           rx == RX_BURN || rx == RX_SHORT ? ST_SPARK : ST_PUFF);
      draw_sprites();
      wait_frames(rx == RX_WEIGHT ? 2u : 5u);
    }
  if (rx == RX_SHORT) shake = 6u;
  puzzle_dirty = 1u; /* a burnt plank leaves a pit: look for a dead room */
  grav_t = 1u; /* a burnt plank drops what stood on it; a weighted plate lifts the shutters */
  return 1;
}

/* --- the bench: an overlay on the window, over the bottom of the room --- */
#define BENCH_Y 72u
static void line(uint8_t y, const char *s) {
  text_at(1, 0, y, "                    ", 4);
  text_at(1, 0, y, s, 4);
}

static void bench_draw(void) {
  uint8_t k, r = 0;
  text_at(1, 0, 0, "POCKET    BENCH     ", 4);
  for (k = 0; k != EG_MAX_ITEM; k++) {
    text_at(1, 0, (uint8_t)(k + 1u), "          ", 4);
    if (k >= pocket_n) {
      text_at(1, 1, (uint8_t)(k + 1u), k ? "" : (pocket_n ? "" : "EMPTY"), 4);
      continue;
    }
    eg_piece_name(pocket[k], buf);
    text_at(1, 1, (uint8_t)(k + 1u), buf, (uint8_t)(sock[0] == k || sock[1] == k ? 5u : 4u));
    if (k == bench_cur) text_at(1, 0, (uint8_t)(k + 1u), ">", 5);
  }
  for (k = 0; k != 2u; k++) {
    text_at(1, 10, (uint8_t)(k + 1u), "          ", 4);
    text_at(1, 10, (uint8_t)(k + 1u), k ? "+" : " ", 4);
    if (sock[k] != 255u) {
      eg_piece_name(pocket[sock[k]], buf);
      text_at(1, 11, (uint8_t)(k + 1u), buf, 5);
    } else
      text_at(1, 11, (uint8_t)(k + 1u), "-", 4);
  }
  text_at(1, 10, 3, "          ", 4);
  if (sock[1] != 255u) {
    r = eg_recipe(pocket[sock[0]], pocket[sock[1]]);
    text_at(1, 10, 3, ">", 5);
    if (r) {
      eg_piece_name(r, buf);
      text_at(1, 11, 3, buf, 5);
    } else
      text_at(1, 11, 3, "NOTHING", 4);
  }
  text_at(1, 10, 4, "          ", 4);
  text_at(1, 10, 5, "          ", 4);
  text_at(1, 10, 6, "          ", 4);
  /* the traits of what the A button would make, else of the piece under the cursor */
  eg_tags_text(r ? r : (pocket_n ? pocket[bench_cur] : 0u), buf);
  line(7, buf);
  line(8, sock[1] != 255u ? "A MAKE  B BACK" : "A ADD B BACK ST USE");
}

static void bench_close(void) {
  bench_on = 0;
  clear_map(1, 4);
  hud_draw();
  hud_charge();
  move_win(7, 128);
  hud_line = 128u;
}

void bench_run(void) BANKED {
  uint8_t a, b, r;
  sock[0] = sock[1] = 255u;
  if (bench_cur >= pocket_n) bench_cur = 0;
  clear_map(1, 4);
  bench_draw();
  hud_line = BENCH_Y;
  move_win(7, BENCH_Y);
  sfx(SFX_STEP);
  bench_on = 1; /* up and listening (the harness waits for this) */
  while (1) {
    wait_frames(1);
    input_poll();
    if (joy_pressed & J_SELECT) break;
    if (joy_pressed & (J_UP | J_DOWN)) {
      if (pocket_n) {
        if (joy_pressed & J_UP) bench_cur = bench_cur ? (uint8_t)(bench_cur - 1u) : (uint8_t)(pocket_n - 1u);
        else bench_cur = (uint8_t)(bench_cur + 1u) >= pocket_n ? 0u : (uint8_t)(bench_cur + 1u);
        sfx(SFX_STEP);
        bench_draw();
      }
    } else if (joy_pressed & J_A) {
      if (sock[1] != 255u) { /* make it */
        a = sock[0];
        b = sock[1];
        r = eg_recipe(pocket[a], pocket[b]);
        if (!r) {
          sfx(SFX_DENY);
          continue;
        }
        pocket_take(a > b ? a : b);
        pocket_take(a > b ? b : a);
        pocket[pocket_n++] = r;
        bench_cur = (uint8_t)(pocket_n - 1u);
        sock[0] = sock[1] = 255u;
        sfx(SFX_CHARGED);
        eg_piece_name(r, buf);
        hud_msg(buf);
        bench_draw();
      } else if (pocket_n && sock[0] != bench_cur) {
        sock[sock[0] == 255u ? 0u : 1u] = bench_cur;
        sfx(SFX_PUSH);
        bench_draw();
      } else
        sfx(SFX_DENY);
    } else if (joy_pressed & J_B) {
      if (sock[0] == 255u) break;
      sock[0] = sock[1] = 255u;
      sfx(SFX_STEP);
      bench_draw();
    } else if ((joy_pressed & J_START) && pocket_n) {
      bench_close();
      bench_on = 2; /* the reaction playing out */
      use(bench_cur);
      bench_on = 0;
      if (bench_cur >= pocket_n) bench_cur = 0;
      return;
    }
  }
  bench_close();
}

/* --- the run planner: the curriculum picks every generated floor's room (src/core/eg_plan.h) --- */
void puzzle_plan_begin(void) BANKED {
  eg_plan_begin(&plan, run_seed, MK_FROM_SEEN(meta.seen), meta.runs > 255u ? 255u : (uint8_t)meta.runs);
}

void puzzle_plan_floor(void) BANKED {
  eg_seed_rec e;
  floor_resets = 0;
  if (dbg_fseed) { /* the harness: this exact room */
    floor_seed = dbg_fseed;
    floor_tier = dbg_ftier;
    dbg_fseed = 0;
    return;
  }
  eg_plan_floor(&plan, floor_no, &e);
  floor_seed = e.seed;
  floor_tier = e.tier;
}

void puzzle_plan_done(void) BANKED {
  eg_plan_result(&plan, floor_resets, acts.frames);
  meta.seen |= MK_TO_SEEN(plan.seen);
}

/* holding SELECT put the room back: it costs a token (never a heart), and the matrix remembers */
void puzzle_reset_cost(void) BANKED {
  if (floor_resets != 255u) floor_resets++;
  if (tokens) tokens--;
  run_room_forgot();
}

/* B2's mobs (and anything else) break fragile floor through here: protected cells refuse, timed kinds come back */
uint8_t puzzle_frag_hit(uint8_t cell, uint8_t who) BANKED {
  uint8_t k = eg_frag_kind_of(cell_type(cell)), j;
  if (!eg_frag_hit(&room, cell, who)) return 0u;
  if (eg_frag[k].reset == FR_TIMED)
    for (j = 0; j != 4u; j++)
      if (!frag_t[j][1]) {
        frag_t[j][0] = cell;
        frag_t[j][1] = eg_frag[k].reset_ticks;
        break;
      }
  queue_cell(cell);
  look_collapse(cell);
  puzzle_dirty = 1u;
  return 1u;
}

/* the hook b1-look's per-tile collapse replaces (a puff and a crack for now) */
void look_collapse(uint8_t cell) BANKED {
  puff((uint8_t)(EG_X(cell) * 16u + 8u), (uint8_t)(EG_Y(cell) * 16u + 8u), 6, ST_PUFF);
  sfx(SFX_BREAK);
}

static uint8_t body_on(uint8_t cell) {
  uint8_t occ[4], n = body(occ), k;
  for (k = 0; k != n; k++)
    if (occ[k] == cell) return 1u;
  return 0u;
}

/* every frame of play: the beat, the player's steps on fragile floor, falls, timed resets, the dead-room hint */
void puzzle_tick(void) BANKED {
  uint8_t c = cell_at(pl_x, pl_y), t, on = (uint8_t)eg_beat_on(frame_count), j;
  if (on != beat_was) { /* the glitched glass flips */
    beat_was = on;
    j = 0;
    do {
      if (cell_type(j) == T_GLITCH) queue_cell(j);
    } while (++j != (uint8_t)EG_N);
  }
  if (c != pl_cell) { /* the centre moved on: the old cell counts once the whole body has left it */
    if (c == frag_pend) frag_pend = 255u; /* stepped back: no step */
    else if (EG_FRAGILE(cell_type(pl_cell)) && !frag_skip) frag_pend = pl_cell; /* (a blink, B3, skips it) */
    pl_cell = c;
  }
  if (frag_pend != 255u && !body_on(frag_pend)) {
    if (eg_step_off(&room, frag_pend, BB_PLAYER, 1u)) {
      look_collapse(frag_pend);
      puzzle_dirty = 1u;
    }
    queue_cell(frag_pend);
    frag_pend = 255u;
  }
  t = cell_type(c);
  if (EG_PASSABLE(t) && !EG_FRAGILE(t) && t != T_GLITCH) pl_safe = c;
  if ((t == T_PIT || (t == T_GLITCH && !on)) && pl_state != PS_EXIT) { /* a fall: hurt, and back to firm floor */
    look_collapse(c);
    player_hurt(1u, pl_x, pl_y);
    psx = cell_sub(EG_X(pl_safe));
    psy = cell_sub(EG_Y(pl_safe));
    pl_x = (uint8_t)(psx >> 4);
    pl_y = (uint8_t)(psy >> 4);
    pl_cell = pl_safe;
    frag_pend = 255u;
    hud_msg("YOU FELL");
  }
  if (!(frame_count & 7u))
    for (j = 0; j != 4u; j++)
      if (frag_t[j][1] && !--frag_t[j][1] && cell_type(frag_t[j][0]) == T_PIT) {
        room.c[frag_t[j][0]] = eg_frag[FK_GLITCH].base;
        queue_cell(frag_t[j][0]);
      }
  if (puzzle_dirty) {
    puzzle_dirty = 0;
    if (!dead_t && eg_dead(&room, c, pocket, pocket_n)) {
      dead_t = 120u;
      hud_msg("ROOM FORGETS"); /* "THE ROOM FORGETS" is 16: the HUD line holds 14 */
      sfx(SFX_TELL);
      shake = 4u;
    }
  }
  if (dead_t > 2u && --dead_t == 2u) {
    hud_msg("HOLD SELECT");
    dead_t = 1u; /* shown once; the reset (floor_build) clears it */
  }
}
