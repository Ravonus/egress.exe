/* The run between floors: what the factions think of you, the truth matrix, the landings (someone talks, then
 * trades), the ending's question, and the battery save.
 *
 * The truth matrix is never rolled. Three questions (WHAT is this place, WHERE is the way out, WHY are you here), three
 * answers each; every floor's acts and every reply you give lean it. The ending asks a question built from what it
 * leans toward. A bounded trace of it survives between runs. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "play.h"
#include "run.h"
#include "mapper.h"
#include "clock.h"
#include "family.h"
#include "resume.h"
#include "narrator.h"
#include "boss_api.h"

uint8_t mods;
int8_t standing[FAC_COUNT];
uint8_t truth[9];
meta_t meta;
uint8_t meta_loaded;
static uint8_t landing_no, met; /* met: factions talked to this run */
static uint8_t run_finished;
void run_resume_get(uint8_t *out2) BANKED {out2[0]=landing_no;out2[1]=met;}
void run_resume_set(const uint8_t *in2) BANKED {landing_no=in2[0];met=in2[1];}

#define TRUTH_MAX 60u
#define POOL_BASE (MOD_LONG | MOD_STATIC | MOD_SURGE)

static void lean(uint8_t i, uint8_t n) {
  uint8_t v = (uint8_t)(truth[i] + n);
  truth[i] = v > TRUTH_MAX ? TRUTH_MAX : v;
}
static void stand(uint8_t f, int8_t d) {
  int8_t v = (int8_t)(standing[f] + d);
  standing[f] = v > 9 ? 9 : (v < -9 ? -9 : v);
}

uint8_t truth_leader(uint8_t axis) BANKED {
  uint8_t a = truth[axis], b = truth[axis + 1u], c = truth[axis + 2u];
  if (a >= b && a >= c) return 0;
  return b >= c ? 1u : 2u;
}

/* --- the save: two records, CRC-16/CCITT, the newer valid one wins --- */
#define SAVE_MAGIC0 'E'
#define SAVE_MAGIC1 'G'
#define SAVE_VER 1u
#define SLOT_SIZE 32u
typedef struct {
  uint8_t magic0, magic1, ver, gen;
  meta_t m;
  uint16_t crc;
} record_t;
static uint8_t save_gen;
uint8_t run_save_serial(void) BANKED {return save_gen;}

static uint16_t crc16(const uint8_t *p, uint8_t n) {
  uint16_t c = 0xffffu;
  uint8_t k;
  while (n--) {
    c ^= (uint16_t)(*p++) << 8;
    for (k = 0; k != 8u; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
  }
  return c;
}

static uint8_t record_ok(const record_t *r) {
  return (uint8_t)(r->magic0 == SAVE_MAGIC0 && r->magic1 == SAVE_MAGIC1 && r->ver == SAVE_VER &&
                   r->crc == crc16((const uint8_t *)r, (uint8_t)(sizeof(record_t) - 2u)));
}

void save_load(void) BANKED {
  record_t a, b;
  uint8_t oka, okb;
  EG_SRAM_OPEN();
  memcpy(&a, (const uint8_t *)0xa000u, sizeof a);
  memcpy(&b, (const uint8_t *)(0xa000u + SLOT_SIZE), sizeof b);
  EG_SRAM_CLOSE();
  oka = record_ok(&a);
  okb = record_ok(&b);
  if (oka && (!okb || (int8_t)(a.gen - b.gen) >= 0)) {
    meta = a.m;
    save_gen = a.gen;
  } else if (okb) {
    meta = b.m;
    save_gen = b.gen;
  } else {
    memset(&meta, 0, sizeof meta);
    meta.unlocked = POOL_BASE;
    save_gen = 0;
    meta_loaded = 0;
    return;
  }
  meta_loaded = 1;
}

/* the next generation goes to the slot the current one is not in, so a lost write leaves the older record */
void save_write(void) BANKED {
  record_t r;
  save_gen++;
  r.magic0 = SAVE_MAGIC0;
  r.magic1 = SAVE_MAGIC1;
  r.ver = SAVE_VER;
  r.gen = save_gen;
  r.m = meta;
  r.crc = crc16((const uint8_t *)&r, (uint8_t)(sizeof(record_t) - 2u));
  EG_SRAM_OPEN();
  memcpy((uint8_t *)(0xa000u + ((save_gen & 1u) ? SLOT_SIZE : 0u)), &r, sizeof r);
  EG_SRAM_CLOSE();
  clock_save(); /* the kept clock rides along with every meta save */
}

/* --- the run --- */
void run_begin(void) BANKED {
  uint8_t i;
  save_load();
  if (!meta.unlocked) meta.unlocked = POOL_BASE;
  pl_hp_max = 6u;
  pl_hp = 6u;
  mods = 0;
  kills_run = 0;heart_shards=0u;
  landing_no = 0;
  met = 0;
  run_finished=0u;
  floor_no=0u;
  memset(standing, 0, sizeof standing);
  /* the trace: half of what the last runs leaned toward */
  for (i = 0; i != 9u; i++) truth[i] = (uint8_t)(meta.trace[i] >> 1);
}

/* what the floor's acts say. Digging into the foam: it is a body. Moving and breaking things: a machine you can
 * rearrange. Deflecting the eyes' light back: a dream you can argue with. Killing: you broke something. Taking a
 * beating and walking on: you have done this before. Taking every token: you are being kept (you keep). */
void run_floor_done(void) BANKED {
  uint8_t kills = (uint8_t)(acts.mites + acts.eyes);
  if (acts.dug >= 24u) {
    lean(TR_WHAT + 2u, 2u);
    stand(FAC_ROOT, -1);
  }
  if (acts.pushes + acts.breaks >= 4u) {
    lean(TR_WHAT + 0u, 2u);
    stand(FAC_COMPILE, 1);
  }
  if (acts.deflects) {
    lean(TR_WHAT + 1u, (uint8_t)(acts.deflects > 3u ? 3u : acts.deflects));
    stand(FAC_SKY, 1);
  }
  if (kills >= 3u) {
    lean(TR_WHY + 0u, 2u);
    stand(FAC_CHARGE, 1);
    stand(FAC_ROOT, -1);
  } else if (!kills && !boss_floor) {
    stand(FAC_CHARGE, -1);
    stand(FAC_SKY, 1);
  }
  if (acts.hits_taken >= 4u) lean(TR_WHY + 2u, 2u);
  if (room.tokens && acts.picked >= room.tokens) {
    lean(TR_WHY + 1u, 1u);
    stand(FAC_LOCK, 1);
  }
  /* quick floors lean the way out downward; slow ones toward nowhere (WATCH likes being looked at) */
  if (acts.frames < 60u * 75u) {
    lean(TR_WHERE + 0u, 2u);
    stand(FAC_CHARGE, 1);
  } else if (acts.frames > 60u * 240u) {
    lean(TR_WHERE + 2u, 2u);
    stand(FAC_WATCH, 1);
  } else
    lean(TR_WHERE + 1u, 1u);
  if (acts.hits_taken == 0u) stand(FAC_WATCH, -1);
  if (floor_no > meta.best_floor) meta.best_floor = floor_no;
  if (acts.mites) meta.seen |= SEEN_MITE;
  if (acts.eyes) meta.seen |= SEEN_EYE;
  if (boss_floor) meta.seen |= SEEN_BOSS;
}

/* B1: the player held SELECT and the room forgot: one small lean of the matrix */
void run_room_forgot(void) BANKED { lean(TR_WHERE + 1u, 1u); }

void run_end(uint8_t won) BANKED {
  uint8_t i, v;
  if (meta.runs != 0xffffu) meta.runs++;
  if (won && meta.wins != 255u) meta.wins++;
  if (floor_no > meta.best_floor) meta.best_floor = floor_no;
  if (acts.mites) meta.seen |= SEEN_MITE;
  if (acts.eyes) meta.seen |= SEEN_EYE;
  if (boss_floor) meta.seen |= SEEN_BOSS;
  /* unlocks: deeper runs put more mods in the traders' pool */
  if (meta.best_floor >= 4u) meta.unlocked |= MOD_GROUND;
  if (meta.best_floor >= 7u) meta.unlocked |= MOD_FUSE;
  if (won) meta.unlocked |= MOD_SPLICE;
  /* the bounded trace: the old trace and this run, averaged, at most 15 */
  for (i = 0; i != 9u; i++) {
    v = (uint8_t)((meta.trace[i] + (truth[i] > 30u ? 30u : truth[i])) >> 1);
    meta.trace[i] = v > 15u ? 15u : v;
  }
  save_write();
}

void run_record_end(uint8_t won) BANKED {
  if(run_finished) return;
  run_finished=1u;
  narrator_event(won ? NE_CLEAR : NE_DEATH,0u);
  resume_save(won ? RES_WON : RES_DEAD);
  run_end(won);family_end(won);resume_clear();
}

/* --- landings --- */
static const char *const fac_name[FAC_COUNT] = {"COMPILE", "CHARGE", "SKY", "WATCH", "ROOT", "LOCK"};

typedef struct {
  const char *l0, *l1, *r0, *r1;
  uint8_t a0, a1; /* the answer (on the landing's question) each reply leans toward */
} talk_t;

/* [faction][question]; question 0 WHAT (an office, a dream, a body), 1 WHERE (down, back, nowhere),
 * 2 WHY (you broke something, you were kept, you came back) */
static const talk_t talks[FAC_COUNT][3] = {
    {/* COMPILE: headsets, tickets, the building as a build */
     {"THIS IS A BUILD.", "WE ARE THE BUILD.", "THEN WHO RUNS IT", "IT'S A DREAM", 0, 1},
     {"EXITS ARE TICKETS.", "FILE ONE. GO DOWN.", "I'LL FILE ONE", "I'LL GO BACK", 0, 1},
     {"YOUR BADGE SAYS", "YOU WORK HERE.", "I QUIT.", "I WAS HIRED?", 0, 1}},
    {/* CHARGE: through, not around */
     {"HIT A WALL. IT", "BRUISES. ALIVE.", "IT'S MEAT", "IT'S DRYWALL", 2, 0},
     {"DOWN. ALWAYS DOWN.", "THROUGH, NOT AROUND", "THROUGH IT", "THERE'S NO OUT", 0, 2},
     {"YOU SWUNG FIRST.", "I SAW THE CARPET.", "IT HAD IT COMING", "I DON'T REMEMBER", 0, 2}},
    {/* SKY: a sheet with nobody under it */
     {"YOU ARE ASLEEP", "AT YOUR DESK.", "THEN WAKE ME", "I FEEL THE FLOOR", 1, 2},
     {"THE DOOR IS", "BEHIND YOUR EYES.", "SO I TURN AROUND", "THAT'S NOWHERE", 1, 2},
     {"YOU LEFT A LIGHT", "ON, ONCE.", "CAME BACK FOR IT", "I WAS LOCKED IN", 2, 1}},
    {/* WATCH: a camera that took a body */
     {"EVERY ROOM IS A", "FRAME. KEEP STILL.", "IT'S A SET", "IT'S A REPLAY", 0, 1},
     {"I HAVE FOOTAGE OF", "YOU LEAVING. NOT.", "PLAY IT BACK", "THERE'S NO TAPE", 1, 2},
     {"WE WERE TOLD TO", "KEEP YOU IN VIEW.", "WHO TOLD YOU", "LOOK AWAY THEN", 1, 0}},
    {/* ROOT: the damp, and what grows on it */
     {"THE WALLS SWEAT.", "WE GROW ON THAT.", "IT'S BREATHING", "IT'S JUST DAMP", 2, 0},
     {"WE GO WHERE THE", "WATER GOES. DOWN.", "FOLLOW THE DRIP", "ROOTS GO NOWHERE", 0, 2},
     {"YOU WERE PLANTED.", "WE WATCHED YOU.", "I WAS PUT HERE", "I GREW BACK", 1, 2}},
    {/* LOCK: keys, rent, the building's promise */
     {"A BUILDING IS A", "PROMISE TO LOCK.", "IT'S PROPERTY", "IT'S A STOMACH", 0, 2},
     {"EVERY DOOR HAS A", "PRICE. SOME, TWO.", "I'LL PAY DOWN", "I'LL GO BACK", 0, 1},
     {"YOU OWE RENT.", "SINCE BEFORE.", "I'M NOT STAYING", "HOW LONG?", 0, 2}},
};
static const char *const hostile[FAC_COUNT] = {"ACCESS DENIED.", "WRONG FLOOR, PAL.", "YOU'RE TOO LOUD.",
                                               "I SAW WHAT YOU DID.", "YOU CUT US.", "NOT ON MY BOOKS."};

/* the place's own voice: what the matrix leans toward on each question */
static const char *const voice[9] = {
    "THE VENTS TYPE.",   "THE CARPET HUMS.", "THE WALLS SWALLOW.", "THE STAIRS LEAN.", "YOUR STEPS ECHO.",
    "THE MAP IS BLANK.", "SOMETHING CRACKED.", "THE LOCKS CLICK.", "YOU KNOW THIS ROOM.",
};

static const char *const mod_name[6] = {"LONG LEAD", "STATIC", "SURGE", "GROUND", "FUSE", "SPLICE"};
static const char *const mod_desc[6] = {"REACH FURTHER", "CHARGE FASTER", "HEAVY 3RD HIT", "ORBS HIT HARD",
                                        "ROLLS SHOCK", "KILLS MEND"};

static void land_reset(void) {
  uint8_t i;
  DISPLAY_OFF;
  HIDE_SPRITES;
  HIDE_WIN;
  hud_line = 144u;
  move_bkg(0, 0);
  for (i = 0; i != 40u; i++) move_sprite(i, 0, 0);
  art_load_game();
  clear_map(0, 4);
  /* text on these screens: palette 4 (cyan-white) and palette 5 (brass) both on the CRT's near-black */
  pal_bg[5 * 4] = pal_bg[4 * 4];
  pal_bg[7 * 4] = pal_bg[4 * 4];
}

static void tile_at(uint8_t x, uint8_t y, uint8_t t, uint8_t pal) {
  uint8_t a = (uint8_t)(pal | 0x08u);
  VBK_REG = 1;
  set_bkg_tiles(x, y, 1, 1, &a);
  VBK_REG = 0;
  set_bkg_tiles(x, y, 1, 1, &t);
}

static void wait_press(void) {
  while (1) {
    frame_wait();
    sound_tick();
    input_poll();
    if (joy_pressed & (J_A | J_START)) return;
  }
}

/* the talker's 32x32 face at (1,1): palettes 6 (rows 0-2) and 7 (row 3); humans get the run's skin and hair */
static void draw_face(uint8_t f) {
  uint8_t r, c, t, a;
  uint16_t p[8];
  if (art_face_palettes(f, p)) { /* (art_face_human lives in the art bank: ask through the call) */
    uint16_t s = (uint16_t)(run_seed * 7u + f * 131u);
    p[1] = people[(uint8_t)(s & 7u)];
    if (f != FAC_CHARGE) p[2] = people[ART_SKINS + (uint8_t)((s >> 3) & 7u)]; /* CHARGE keeps the red mohawk */
  }
  memcpy(&pal_bg[6 * 4], p, 16);
  for (r = 0; r != 4u; r++)
    for (c = 0; c != 4u; c++) {
      t = (uint8_t)(BT_FACE + f * 16u + r * 4u + c);
      a = (uint8_t)((r == 3u ? 7u : 6u) | 0x08u);
      VBK_REG = 1;
      set_bkg_tiles((uint8_t)(1u + c), (uint8_t)(1u + r), 1, 1, &a);
      VBK_REG = 0;
      set_bkg_tiles((uint8_t)(1u + c), (uint8_t)(1u + r), 1, 1, &t);
    }
}

/* up/down between n rows starting at y; A picks, B (if allowed) returns 255 */
static uint8_t choose(uint8_t y, uint8_t n, uint8_t allow_b) {
  uint8_t sel = 0, k;
  while (1) {
    for (k = 0; k != n; k++) text_at(0, 1, (uint8_t)(y + k), k == sel ? ">" : " ", 5);
    frame_wait();
    sound_tick();
    input_poll();
    if ((joy_pressed & J_UP) && sel) {
      sel--;
      sfx(SFX_STEP);
    }
    if ((joy_pressed & J_DOWN) && sel + 1u < n) {
      sel++;
      sfx(SFX_STEP);
    }
    if (joy_pressed & J_A) return sel;
    if (allow_b && (joy_pressed & J_B)) return 255u;
  }
}

static uint8_t price_of(uint8_t base, uint8_t f) {
  int8_t p = (int8_t)((int8_t)base - standing[f] / 2);
  return p < 1 ? 1u : (uint8_t)p;
}

static void trade(uint8_t f) {
  uint8_t offer[3], price[3], k, n = 0, b, pool, bought = 0, sel;
  uint16_t s = (uint16_t)(run_seed ^ ((uint16_t)landing_no << 9) ^ f);
  /* two mods you haven't installed, from the pool the save has unlocked (while a slot is free) */
  pool = (uint8_t)(meta.unlocked & (uint8_t)~mods);
  {
    uint8_t m = mods, used = 0;
    while (m) {
      used += (uint8_t)(m & 1u);
      m >>= 1;
    }
    if (used >= MOD_SLOTS) pool = 0;
  }
  for (k = 0; k != 6u && n != 2u; k++) {
    b = (uint8_t)((s + k) % 6u);
    if (pool & (uint8_t)(1u << b)) {
      pool &= (uint8_t)~(1u << b);
      offer[n] = b;
      price[n++] = price_of(14u, f);
    }
  }
  if (n != 2u && pl_hp_max < 10u) {
    offer[n] = 6u; /* BATTERY */
    price[n++] = price_of(12u, f);
  }
  offer[n] = 7u; /* COFFEE */
  price[n++] = price_of(5u, f);
  while (1) {
    clear_map(0, 4);
    draw_face(f);
    text_at(0, 6, 1, fac_name[f], 4);
    text_at(0, 6, 2, "TRADES", 4);
    tile_at(6, 4, BT_ICON_TOKEN, 5);
    num_at(0, 8, 4, tokens, 3, 4);
    for (k = 0; k != n; k++) {
      uint8_t y = (uint8_t)(7u + k * 2u);
      if (offer[k] == 0xffu) {
        text_at(0, 2, y, "SOLD", 4);
      } else if (offer[k] < 6u) {
        text_at(0, 2, y, mod_name[offer[k]], 4);
        text_at(0, 3, (uint8_t)(y + 1u), mod_desc[offer[k]], 5);
      } else if (offer[k] == 6u) {
        text_at(0, 2, y, "BATTERY", 4);
        text_at(0, 3, (uint8_t)(y + 1u), "+1 HEART", 5);
      } else {
        text_at(0, 2, y, "COFFEE", 4);
        text_at(0, 3, (uint8_t)(y + 1u), "MEND 1 HEART", 5);
      }
      if (offer[k] != 0xffu) num_at(0, 16, y, price[k], 2, 4);
    }
    text_at(0, 2, 15, "A BUY  B LEAVE", 4);
    pal_commit(8);
    sel = 0;
    /* the cursor rows are two apart: choose over n with doubled rows */
    while (1) {
      for (k = 0; k != n; k++) text_at(0, 1, (uint8_t)(7u + k * 2u), k == sel ? ">" : " ", 5);
      frame_wait();
      sound_tick();
      input_poll();
      if ((joy_pressed & J_UP) && sel) sel--;
      if ((joy_pressed & J_DOWN) && sel + 1u < n) sel++;
      if (joy_pressed & J_B) goto done;
      if (joy_pressed & J_A) break;
    }
    if (tokens < price[sel] || offer[sel] == 0xffu) {
      sfx(SFX_DENY);
      continue;
    }
    tokens = (uint8_t)(tokens - price[sel]);
    sfx(SFX_TOKEN);
    bought = 1;
    stand(f, 1);
    if (offer[sel] < 6u) {
      mods |= (uint8_t)(1u << offer[sel]);
      offer[sel] = 0xffu;
      price[sel] = 99u;
    } else if (offer[sel] == 6u) {
      pl_hp_max = (uint8_t)(pl_hp_max + 2u);
      pl_hp = (uint8_t)(pl_hp + 2u);
      offer[sel] = 0xffu;
      price[sel] = 99u;
    } else if (pl_hp < pl_hp_max) {
      pl_hp = (uint8_t)(pl_hp + 2u > pl_hp_max ? pl_hp_max : pl_hp + 2u);
    }
  }
done:
  /* LOCK keeps the books: spending is suspicious, saving is a virtue */
  if (f != FAC_LOCK) stand(FAC_LOCK, bought ? -1 : 1);
}

void landing_screen(void) BANKED {
  uint8_t f, q, k, lead, pick, i;
  const talk_t *t;
  landing_no++;
  q = (uint8_t)((landing_no - 1u) % 3u); /* the first landing asks WHAT, the second WHERE, the third WHY */
  /* the talker: seeded by the run, never the same faction twice in a run */
  f = (uint8_t)(((run_seed >> 5) + landing_no * 5u) % FAC_COUNT);
  for (k = 0; k != FAC_COUNT && (met & (uint8_t)(1u << f)); k++) f = (uint8_t)((f + 1u) % FAC_COUNT);
  met |= (uint8_t)(1u << f);
  meta.seen |= (uint16_t)(SEEN_TALKER0 << f);
  /* the matrix drifts back a little at every landing: nothing stays settled */
  for (i = 0; i != 9u; i++)
    if (truth[i]) truth[i]--;
  lead = truth_leader((uint8_t)(q * 3u));
  t = &talks[f][q];
  game_mode = MODE_CLEAR;
  land_reset();
  draw_face(f);
  text_at(0, 6, 1, fac_name[f], 4);
  text_at(0, 6, 2, "LANDING", 5);
  num_at(0, 14, 2, floor_no, 2, 5);
  if (standing[f] <= -4) {
    text_at(0, 1, 7, hostile[f], 4);
    text_at(0, 1, 15, voice[q * 3u + lead], 5);
    pal_commit(0);
    SHOW_BKG;
    DISPLAY_ON;
    fade_to(0, 8);
    wait_press();
    fade_to(8, 0);
    return;
  }
  text_at(0, 1, 6, t->l0, 4);
  text_at(0, 1, 7, t->l1, 4);
  text_at(0, 2, 10, t->r0, 4);
  text_at(0, 2, 11, t->r1, 4);
  text_at(0, 1, 15, voice[q * 3u + lead], 5);
  pal_commit(0);
  SHOW_BKG;
  DISPLAY_ON;
  fade_to(0, 8);
  pick = choose(10, 2, 0);
  sfx(SFX_KEY);
  if (pick == 0u) {
    lean((uint8_t)(q * 3u + t->a0), 4u);
    stand(f, 2);
  } else {
    lean((uint8_t)(q * 3u + t->a1), 4u);
    stand(f, -1);
  }
  trade(f);
  fade_to(8, 0);
}

/* --- the ending: a question, from what the matrix leans toward on WHAT and WHY --- */
static const char *const question[9][3] = {
    {"IF YOU BROKE IT,", "WHO FILES THE", "REPORT?"},
    {"WHO SIGNED YOU IN,", "AND WHY IS THE", "NAME YOURS?"},
    {"HOW MANY SHIFTS", "HAVE YOU WORKED", "THIS ONE?"},
    {"IF YOU WAKE,", "WHAT STAYS", "BROKEN?"},
    {"WHO IS DREAMING", "THAT YOU", "CAN'T LEAVE?"},
    {"HOW OFTEN HAVE", "YOU WOKEN HERE", "BEFORE?"},
    {"WHAT DID YOU", "CUT YOUR WAY", "OUT OF?"},
    {"WHAT WAS", "DIGESTING", "YOU?"},
    {"WHY DOES IT", "KNOW YOUR", "SHAPE?"},
};

void ending_screen(void) BANKED {
  uint8_t w = truth_leader(TR_WHAT), y = truth_leader(TR_WHY), k;
  const char *const *qq = question[w * 3u + y];
  game_mode = MODE_CLEAR;
  land_reset();
  text_at(0, 2, 2, "THE EGRESS OPENS", 7);
  text_at(0, 2, 3, "ONTO A LANDING.", 7);
  for (k = 0; k != 3u; k++) text_at(0, 2, (uint8_t)(6u + k), qq[k], 4);
  text_at(0, 2, 12, "RUNS", 5);
  num_at(0, 9, 12, meta.runs, 3, 5);
  text_at(0, 2, 13, "OUT", 5);
  num_at(0, 9, 13, meta.wins, 3, 5);
  text_at(0, 2, 16, "PRESS A", 4);
  pal_commit(0);
  SHOW_BKG;
  DISPLAY_ON;
  music_play(SONG_LOBBY);
  music_fade(0);
  fade_to(0, 8);
  for (k = 0; k != 60u; k++) {
    frame_wait();
    sound_tick();
  }
  wait_press();
  music_fade(1);
  fade_to(8, 0);
}
