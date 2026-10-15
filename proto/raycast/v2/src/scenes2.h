/* scene scripts (included by main2.c). Scenes 1-3 are pure functions of a looping clock, so a GIF window and the
   measured window always show the same beats. */
#if SCENE == 0 || SCENE == 4
static void scene_init(void) {
}
static void scene_update(uint8_t dt) {
  walker_update(dt);
}
#else

uint16_t sclock; /* global so measure2.py can log the beat */
/* looping clock without a division: dt is a few views at most */
#define SCLOCK_STEP(dt) do { sclock += (dt); while (sclock >= LOOP) sclock -= LOOP; } while (0)

/* raw map: 1 wall, 0 open, 2 door; dress() then textures it */
static void box(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t v) {
  uint8_t x, y;
  for (y = y0; y <= y1; y++)
    for (x = x0; x <= x1; x++) map[(uint16_t)y * MAPW + x] = v;
}

/* a -> b over clock t0..t1 in 1/64 steps; t0/t1 are constants, so the 1/(t1 - t0) is folded at compile time and
   the frame loop only multiplies (no 16-bit division per call) */
static uint16_t lerp_f(uint16_t a, uint16_t b, uint8_t f) {
  /* (b - a) in 1/16 cell so d * f stays inside int16 for spans up to ~30 cells */
  int16_t d = (int16_t)(((int16_t)b - (int16_t)a) >> 4);
  return (uint16_t)((int16_t)a + (int16_t)((d * (int16_t)f) >> 2));
}
#define lerp16(a, b, t, t0, t1) ((t) <= (t0) ? (a) : (t) >= (t1) ? (b) : \
  lerp_f((a), (b), (uint8_t)(((uint16_t)((t) - (t0)) * (uint16_t)(16384u / ((t1) - (t0)))) >> 8)))

#define CELL(x) ((uint16_t)((x) * 256.0))

#if SCENE == 1
/* combat: a mob, an elite and a second mob come down an office room; the heir sidesteps a tell, strikes, kills the
   first, parries the elite's wind-up, charges it, then backsteps the second mob. 8 s loop.
   Spawns are staggered for the CGB's 10-sprites-per-line limit: billboards share the horizon rows, so the columns
   of every mob in view (plus the weapon's 2) must stay <= 10. The elite waits deep in the room (small) until the
   first mob is down, and the second mob only enters after that. */
#define LOOP 480u
#ifndef ROOMX
#define ROOMX 14 /* far wall of the room (cells) */
#endif
static void scene_init(void) {
  box(0, 0, 31, 31, 1);
  box(3, 3, ROOMX, 9, 0);
  map[6u * MAPW + ROOMX + 1u] = 2;
  dress();
  cam_a = 0;
}

static void mob_at(uint8_t i, uint8_t var, uint16_t x, uint16_t y) {
  mob[i].on = 1;
  mob[i].var = var;
  mob[i].x = x;
  mob[i].y = y;
  mob[i].fx = FX_NONE;
  mob[i].fxt = 0;
}

static void scene_update(uint8_t dt) {
  uint16_t t;
  uint8_t walk;
  if (cam_lock) return;
  SCLOCK_STEP(dt);
  t = sclock;
  walk = (t >> 3) & 1u ? BB_WALK1 : BB_WALK0;
  /* camera: sidestep (B) at 150, lunge with the strikes at 165-185, backstep at 425 */
  cam_x = lerp16(CELL(4.5), CELL(4.9), t, 165u, 185u);
  if (t >= 410u) cam_x = lerp16(CELL(4.9), CELL(4.3), t, 425u, 440u);
  cam_y = t < 240u ? lerp16(CELL(6.5), CELL(5.9), t, 150u, 162u) : lerp16(CELL(5.9), CELL(6.5), t, 240u, 260u);
  cam_a = 0;
  /* mob 0: approaches, tells at 110, attacks at 150 (dodged), is hit at 172 and 192, dies, gone at 228 */
  mob_at(0, 0, lerp16(CELL(ROOMX - 2.5), CELL(6.5), t, 0u, 110u), CELL(5.7));
  mob[0].frame = walk;
  if (t >= 110u && t < 150u) { mob[0].frame = BB_TELL; mob[0].fx = FX_TELL; }
  else if (t >= 150u && t < 166u) mob[0].frame = BB_ATTACK;
  else if ((t >= 172u && t < 182u) || (t >= 192u && t < 200u)) { mob[0].frame = BB_HURT; mob[0].fx = FX_HIT; }
  else if (t >= 200u) {
    mob[0].frame = t < 212u ? BB_DIE0 : BB_DIE1;
    mob[0].fx = FX_DEAD;
    mob[0].fxt = (uint8_t)((t - 200u) >> 1);
    if (t >= 228u) mob[0].on = 0;
  }
  /* the elite: holds deep in the room, closes in once mob 0 is down, long tell at 250, parried at 292 (white
     flash + stagger), charged at 306-360 */
  mob_at(2, 2, t < 228u ? lerp16(CELL(ROOMX - 0.5), CELL(ROOMX - 2.0), t, 40u, 228u)
                        : lerp16(CELL(ROOMX - 2.0), CELL(7.0), t, 228u, 250u), CELL(6.6));
  mob[2].frame = (t >> 4) & 1u ? BB_WALK1 : BB_WALK0;
  if (t >= 250u && t < 292u) { mob[2].frame = BB_TELL; mob[2].fx = FX_TELL; }
  else if (t >= 292u && t < 306u) { mob[2].frame = BB_HURT; mob[2].fx = FX_HIT; }
  else if (t >= 306u && t < 360u) {
    mob[2].frame = ((t - 306u) & 15u) < 6u ? BB_HURT : BB_WALK0;
    mob[2].fx = ((t - 306u) & 15u) < 6u ? FX_HIT : FX_NONE;
  }
  if (t >= 360u) mob[2].on = 0; /* broken and gone: the charge finished it */
  /* mob 1: enters through the far door after the elite is engaged, tells at 380, attacks at 420 (backstepped) */
  mob_at(1, 1, lerp16(CELL(ROOMX - 0.5), CELL(7.3), t, 300u, 380u), CELL(6.9));
  mob[1].frame = walk;
  if (t < 300u) mob[1].on = 0;
  else if (t >= 380u && t < 420u) { mob[1].frame = BB_TELL; mob[1].fx = FX_TELL; }
  else if (t >= 420u && t < 438u) mob[1].frame = BB_ATTACK;
  /* weapon: strikes at 168, 188 (kill), the parry at 288, the charge 306-356 */
  weapon_frame = 0;
  if ((t >= 168u && t < 176u) || (t >= 188u && t < 196u) || (t >= 288u && t < 296u) ||
      (t >= 306u && t < 356u && ((t - 306u) & 15u) < 6u))
    weapon_frame = 1;
}

#elif SCENE == 2
/* puzzle: a corridor ends in a poster wall; the heir uses a HOT recipe on it, the paper burns through three
   frames, chars, and the cell opens onto the room behind. The wall tile is the reaction. 7 s loop. */
#define LOOP 420u
#define PX 13u
#define PY 6u
static void scene_init(void) {
  box(0, 0, 31, 31, 1);
  box(3, 6, 12, 6, 0);
  box(14, 3, 19, 9, 0);
  dress();
  map[PY * MAPW + PX] = O_POSTER;
  cam_a = 0;
  mob[0].on = mob[1].on = mob[2].on = 0;
}

static void scene_update(uint8_t dt) {
  uint16_t t;
  uint8_t c;
  if (cam_lock) return;
  SCLOCK_STEP(dt);
  t = sclock;
  cam_y = CELL(6.5);
  cam_a = 0;
  if (t < 250u) cam_x = lerp16(CELL(4.0), CELL(11.2), t, 0u, 120u);
  else cam_x = lerp16(CELL(11.2), CELL(17.0), t, 270u, 400u);
  weapon_frame = (t >= 130u && t < 140u) ? 1u : 0u;
  if (t < 140u) c = O_POSTER;
  else if (t < 160u) c = O_BURN0;
  else if (t < 180u) c = O_BURN1;
  else if (t < 200u) c = O_BURN2;
  else if (t < 245u) c = O_CHAR;
  else c = (uint8_t)(0x80u | S_OFFICE);
  if (map[PY * MAPW + PX] != c) {
    map[PY * MAPW + PX] = c;
#if FS
    map_ver++;
#endif
  }
}

#elif SCENE == 3
/* biome blend: one long corridor from the office into the vents; walls and floors dither across a 3-cell
   threshold (dress() by column) while the palettes crossfade with the camera. 8 s loop. */
#define LOOP 480u
static void scene_init(void) {
  uint8_t x;
  box(0, 0, 31, 31, 1);
  box(2, 15, 29, 15, 0);
  for (x = 4; x < 28u; x += 5u) map[14u * MAPW + x] = 0; /* side niches */
  for (x = 6; x < 28u; x += 7u) map[16u * MAPW + x] = 0;
  dress();
  cam_a = 0;
  mob[0].on = mob[1].on = mob[2].on = 0;
}

static uint8_t blend_t = 0xFFu;
static void scene_update(uint8_t dt) {
  uint16_t t;
  uint8_t b;
  if (cam_lock) return;
  SCLOCK_STEP(dt);
  t = sclock;
  cam_x = lerp16(CELL(3.0), CELL(26.0), t, 0u, LOOP - 20u);
  cam_y = CELL(15.5);
  cam_a = 0;
  weapon_frame = 0;
  /* palette: office until the threshold starts (x 14), vents after it (x 17) */
  b = cam_x < CELL(13.0) ? 0u : (cam_x >= CELL(17.0) ? 16u : (uint8_t)((cam_x - CELL(13.0)) >> 6));
  if (b != blend_t) {
    blend_t = b;
    pal_blend(PAL_OFFICE, PAL_VENTS, b);
  }
}
#endif
#endif
