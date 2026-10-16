/* Sound: a small four-channel score player and the effects.
 *
 * Scores (gen/music_data.h, written by tools/compose.py) are four event streams of (note, rows) pairs: pulse 1 holds a
 * soft pad, pulse 2 the lead with a quiet echo a few rows behind it, the wave channel a round bass, and noise brushes
 * and hats. Notes are pitch indices from C2; MN_REST silences, MN_HOLD lets the note ring, MN_END loops the stream.
 * Effects borrow pulse 1 and noise; the music on those channels resumes at its next note. Music volume scales the
 * score's envelopes, SFX volume the effects'; fades ramp the master level. */
#pragma bank 255
#include "voice.h"
#include <gb/gb.h>
#include <gb/hardware.h>
#include "egress.h"
#include "gen/music_data.h"

uint8_t music_volume = 6u; /* of 8 */
uint8_t sfx_volume = 7u;   /* of 8 */
uint8_t music_song;

static uint8_t fpr, row_wait, fade_level, fade_target, fade_t;
static const uint8_t *stream[4];
static uint16_t at[4];
static uint8_t left[4];
static uint8_t echo_t, echo_note, lead_env, pad_env, lead_duty, pad_duty, wave_vol, echo_rows;

/* effects */
typedef struct {
  const uint8_t *ch1, *ch4;
} sfx_def;
#define NOTE(fr, env, n, duty) fr, env, n, duty
#define NZ(fr, env, poly) fr, env, poly, 0
#define END 0
enum {
  C4 = 24, D4 = 26, E4 = 28, F4 = 29, G4 = 31, A4 = 33, B4 = 35, C5 = 36, D5 = 38, E5 = 40, F5 = 41, G5 = 43,
  A5 = 45, B5 = 47, C6 = 48, D6 = 50, E6 = 52, G6 = 55, B6 = 59, C7 = 60, F3 = 17, C3 = 12,
  D3 = 14, A3 = 21, FS4 = 30, FS5 = 42, FS6 = 54, A6 = 57, D7 = 62
};
static const uint8_t s_step4[] = {NZ(2, 0x21, 0x6c), END};
static const uint8_t s_dig4[] = {NZ(3, 0x51, 0x5d), NZ(4, 0x31, 0x6e), END};
static const uint8_t s_token1[] = {NOTE(3, 0x81, E6, 2), NOTE(8, 0x73, B6, 2), END};
static const uint8_t s_key1[] = {NOTE(4, 0x92, C6, 2), NOTE(4, 0x92, E6, 2), NOTE(4, 0x92, G6, 2),
                                 NOTE(10, 0x94, C7, 2), END};
static const uint8_t s_door1[] = {NOTE(6, 0x84, G4, 2), NOTE(6, 0x84, D5, 2), NOTE(6, 0x84, G5, 2),
                                  NOTE(16, 0x86, A5, 2), END};
static const uint8_t s_lash4[] = {NZ(3, 0x71, 0x24), NZ(4, 0x41, 0x35), END};
static const uint8_t s_hit1[] = {NOTE(2, 0xb1, C5, 1), NOTE(3, 0x81, G4, 1), END},
                     s_hit4[] = {NZ(3, 0xa1, 0x50), END};
static const uint8_t s_hurt1[] = {NOTE(4, 0xc2, F4, 2), NOTE(4, 0xa2, C4, 2), NOTE(8, 0x83, F3, 2), END};
static const uint8_t s_roll4[] = {NZ(6, 0x52, 0x46), NZ(6, 0x32, 0x57), END};
static const uint8_t s_charged1[] = {NOTE(3, 0x71, C6, 2), NOTE(3, 0x71, G6, 2), NOTE(8, 0x93, C7, 2), END};
static const uint8_t s_spin4[] = {NZ(4, 0x93, 0x23), NZ(6, 0x73, 0x34), NZ(8, 0x54, 0x45), END};
static const uint8_t s_break4[] = {NZ(4, 0xc2, 0x61), NZ(8, 0x93, 0x72), END};
static const uint8_t s_thud4[] = {NZ(3, 0xa1, 0x71), NZ(8, 0x72, 0x77), END};
static const uint8_t s_push4[] = {NZ(6, 0x62, 0x6f), END};
static const uint8_t s_tell1[] = {NOTE(5, 0x62, A5, 0), NOTE(5, 0x62, C6, 0), NOTE(8, 0x52, E6, 0), END};
static const uint8_t s_shot1[] = {NOTE(3, 0x91, E6, 2), NOTE(6, 0x72, B5, 2), END};
static const uint8_t s_kill1[] = {NOTE(3, 0x81, G5, 2), NOTE(8, 0x73, D6, 2), END},
                     s_kill4[] = {NZ(4, 0xb2, 0x52), END};
static const uint8_t s_deny1[] = {NOTE(4, 0x82, C4, 3), NOTE(6, 0x72, C4, 3), END};
static const uint8_t s_exit1[] = {NOTE(6, 0x93, C5, 2), NOTE(6, 0x93, E5, 2), NOTE(6, 0x93, G5, 2),
                                  NOTE(6, 0x93, B5, 2), NOTE(20, 0x95, C6, 2), END};
/* the fragile floors give way, in the floors' D major: worn carpet a low thud and a tearing rasp; a ceiling tile a
 * gritty crunch over a dull knock; glass a bright shatter with falling D-major glints; glitched glass a zap that
 * stutters down an arpeggio */
static const uint8_t s_tear1[] = {NOTE(3, 0xa1, D3, 2), NOTE(6, 0x72, A3, 2), END};
static const uint8_t s_tear4[] = {NZ(3, 0xb1, 0x75), NZ(3, 0x72, 0x45), NZ(4, 0x62, 0x55), NZ(8, 0x43, 0x46), END};
static const uint8_t s_crunch1[] = {NOTE(3, 0x81, A3, 1), NOTE(5, 0x61, D4, 1), END};
static const uint8_t s_crunch4[] = {NZ(2, 0xc1, 0x53), NZ(3, 0x91, 0x64), NZ(2, 0x81, 0x52), NZ(8, 0x63, 0x65), END};
static const uint8_t s_shatter1[] = {NOTE(2, 0x91, A6, 0), NOTE(2, 0x81, FS6, 0), NOTE(2, 0x81, D7, 0), NOTE(3, 0x71, A6, 0),
                                     NOTE(3, 0x61, FS6, 0), NOTE(8, 0x53, D6, 0), END};
static const uint8_t s_shatter4[] = {NZ(2, 0xd1, 0x10), NZ(6, 0x93, 0x21), NZ(10, 0x54, 0x22), END};
static const uint8_t s_zap1[] = {NOTE(1, 0xa1, D7, 3), NOTE(1, 0xa1, A6, 3), NOTE(1, 0x91, FS6, 3), NOTE(1, 0x91, D6, 3),
                                 NOTE(2, 0x81, A5, 3), NOTE(1, 0x81, D7, 3), NOTE(2, 0x71, FS5, 3), NOTE(6, 0x63, D5, 3),
                                 END};
static const uint8_t s_zap4[] = {NZ(2, 0x91, 0x1b), NZ(2, 0x00, 0x1b), NZ(3, 0x72, 0x2b), END};
/* B2 bosses: a stamp lifting (a rising whine), a sweep's wind (a hiss that swells), the beam charging (a climbing
 * buzz), a shelf's crack (a long low rumble), the phase roar (a falling growl over noise), the clink of a closed
 * guard, the fax's rattle before a ram, its jam's buzz, and the boss going down (a slow falling D-major line) */
static const uint8_t s_brise1[] = {NOTE(4, 0x71, D4, 1), NOTE(4, 0x71, FS4, 1), NOTE(4, 0x71, A4, 1), NOTE(6, 0x73, D5, 1), END};
static const uint8_t s_bwhoosh4[] = {NZ(4, 0x2f, 0x24), NZ(8, 0x6f, 0x23), NZ(6, 0x71, 0x22), END};
static const uint8_t s_bbeam1[] = {NOTE(3, 0x81, D5, 3), NOTE(3, 0x81, FS5, 3), NOTE(3, 0x81, A5, 3), NOTE(3, 0x81, D6, 3),
                                   NOTE(3, 0x81, FS6, 3), NOTE(8, 0x84, A6, 3), END};
static const uint8_t s_brumble4[] = {NZ(8, 0x9f, 0x77), NZ(12, 0x97, 0x76), NZ(12, 0x75, 0x77), END};
static const uint8_t s_broar1[] = {NOTE(6, 0xa1, A3, 3), NOTE(6, 0x91, F3, 3), NOTE(8, 0x81, D3, 3), NOTE(12, 0x73, C3, 3), END};
static const uint8_t s_broar4[] = {NZ(10, 0xa2, 0x67), NZ(16, 0x85, 0x77), END};
static const uint8_t s_bclink1[] = {NOTE(2, 0x91, D7, 0), NOTE(5, 0x62, A6, 0), END};
static const uint8_t s_bram4[] = {NZ(2, 0x81, 0x45), NZ(2, 0x00, 0x45), NZ(2, 0x81, 0x45), NZ(2, 0x00, 0x45),
                                  NZ(2, 0x91, 0x44), NZ(2, 0x00, 0x44), NZ(6, 0x93, 0x43), END};
static const uint8_t s_bjam1[] = {NOTE(2, 0x81, A4, 3), NOTE(2, 0x00, A4, 3), NOTE(2, 0x81, A4, 3), NOTE(2, 0x00, A4, 3),
                                  NOTE(2, 0x91, D5, 3), NOTE(2, 0x00, D5, 3), NOTE(4, 0x91, D5, 3), END};
static const uint8_t s_bdown1[] = {NOTE(10, 0x93, D6, 2), NOTE(10, 0x93, A5, 2), NOTE(10, 0x93, FS5, 2), NOTE(10, 0x93, D5, 2),
                                   NOTE(24, 0x97, D4, 2), END};
static const uint8_t s_bdown4[] = {NZ(24, 0xa7, 0x77), END};
/* end B2 bosses */
static const sfx_def sfx_table[SFX_COUNT] = {
    {0, 0},          {0, s_step4},        {0, s_dig4},     {s_token1, 0}, {s_key1, 0},    {s_door1, 0},
    {0, s_lash4},    {s_hit1, s_hit4},    {s_hurt1, 0},    {0, s_roll4},  {s_charged1, 0}, {0, s_spin4},
    {0, s_break4},   {0, s_thud4},        {0, s_push4},    {s_tell1, 0},  {s_shot1, 0},   {s_kill1, s_kill4},
    {s_deny1, 0},    {s_exit1, 0},
    {s_tear1, s_tear4}, {s_crunch1, s_crunch4}, {s_shatter1, s_shatter4}, {s_zap1, s_zap4},
    /* B2 bosses */
    {s_brise1, 0}, {0, s_bwhoosh4}, {s_bbeam1, 0}, {0, s_brumble4}, {s_broar1, s_broar4}, {s_bclink1, 0},
    {0, s_bram4}, {s_bjam1, 0}, {s_bdown1, s_bdown4},
};
static const uint8_t *fx[2];
static uint8_t fx_left[2];

static uint8_t scale_env(uint8_t env, uint8_t vol) {
  uint8_t v = (uint8_t)(env >> 4);
  if (!v) return env;
  v = (uint8_t)((v * vol + 4u) >> 3);
  if (!v) v = 1u;
  return (uint8_t)((v << 4) | (env & 15u));
}

static void pulse(uint8_t ch, uint8_t note, uint8_t env, uint8_t duty) {
  uint16_t f = mn_period[note];
  if (ch == 0u) {
    NR10_REG = 0;
    NR11_REG = (uint8_t)(duty << 6);
    NR12_REG = env;
    NR13_REG = (uint8_t)f;
    NR14_REG = (uint8_t)(0x80u | (f >> 8));
  } else {
    NR21_REG = (uint8_t)(duty << 6);
    NR22_REG = env;
    NR23_REG = (uint8_t)f;
    NR24_REG = (uint8_t)(0x80u | (f >> 8));
  }
}

static void noise(uint8_t env, uint8_t poly) {
  NR41_REG = 0;
  NR42_REG = env;
  NR43_REG = poly;
  NR44_REG = 0x80u;
}

void sound_init(void) BANKED {
  NR52_REG = 0x80u;
  NR51_REG = 0xffu;
  NR50_REG = 0x77u;
  fade_level = fade_target = 7u;
  music_song = SONG_NONE;
  voice_init();
}

void sfx(uint8_t id) BANKED {
  const sfx_def *d;
  if (id >= SFX_COUNT) return;
  d = &sfx_table[id];
  if (d->ch1) {
    fx[0] = d->ch1;
    fx_left[0] = 0;
  }
  if (d->ch4) {
    fx[1] = d->ch4;
    fx_left[1] = 0;
  }
}

static void load_wave(const uint8_t *w) {
  if(voice_busy) return;
  uint8_t i;
  NR30_REG = 0;
  for (i = 0; i != 16u; i++) (&AUD3WAVE[0])[i] = w[i];
  NR30_REG = 0x80u;
}

void music_play(uint8_t song) BANKED {
  const uint8_t *h;
  uint8_t i;
  if (song == music_song) return;
  voice_stop();
  music_song = song;
  NR12_REG = NR22_REG = NR42_REG = 0;
  NR32_REG = 0;
  if (song == SONG_NONE) return;
  h = mn_songs[song - 1u];
  fpr = h[0];
  pad_duty = h[1];
  pad_env = h[2];
  lead_duty = h[3];
  lead_env = h[4];
  wave_vol = h[5];
  echo_rows = h[6];
  load_wave(mn_waves[h[7]]);
  for (i = 0; i != 4u; i++) {
    stream[i] = mn_streams[(uint8_t)((song - 1u) * 4u + i)];
    at[i] = 0;
    left[i] = 0;
  }
  row_wait = 1u;
  echo_t = 0;
}

void music_fade(uint8_t out) BANKED {
  fade_target = out ? 0u : 7u;
  fade_t = 0;
}

static void channel_row(uint8_t c) {
  uint8_t note, dur;
  if (left[c]) {
    left[c]--;
    if (left[c]) return;
  }
  note = stream[c][at[c]];
  if (note == MN_END) {
    at[c] = 0;
    note = stream[c][0];
  }
  dur = stream[c][at[c] + 1u];
  at[c] += 2u;
  left[c] = dur;
  if (note == MN_HOLD) return;
  switch (c) {
    case 0:
      if (fx[0]) return;
      if (note == MN_REST)
        NR12_REG = 0;
      else
        pulse(0, note, scale_env(pad_env, music_volume), pad_duty);
      break;
    case 1:
      if (note == MN_REST) {
        NR22_REG = 0;
        NR24_REG = 0x80u;
      } else {
        pulse(1, note, scale_env(lead_env, music_volume), lead_duty);
        echo_note = note;
        echo_t = (uint8_t)(echo_rows * fpr);
      }
      break;
    case 2:
      if(voice_busy) return;
      if (note == MN_REST || !music_volume)
        NR32_REG = 0;
      else {
        uint16_t f = mn_period[note];
        NR30_REG = 0x80u;
        NR32_REG = music_volume >= 5u ? wave_vol : 0x60u;
        NR33_REG = (uint8_t)f;
        NR34_REG = (uint8_t)(0x80u | (f >> 8));
      }
      break;
    default:
      if (fx[1] || note == MN_REST) return;
      noise(scale_env(mn_noise[note][0], music_volume), mn_noise[note][1]);
      break;
  }
}

static void fx_tick(uint8_t c) {
  const uint8_t *p = fx[c];
  if (!p) return;
  if (fx_left[c]) {
    fx_left[c]--;
    if (fx_left[c]) return;
    p += 4;
  }
  if (!p[0]) {
    fx[c] = 0;
    if (c == 0u) NR12_REG = 0;
    else NR42_REG = 0;
    NR14_REG = 0x80u;
    return;
  }
  fx[c] = p;
  fx_left[c] = p[0];
  if (c == 0u)
    pulse(0, p[2], scale_env(p[1], sfx_volume), p[3]);
  else
    noise(scale_env(p[1], sfx_volume), p[2]);
}

void sound_tick(void) BANKED {
  uint8_t c;
  if(voice_dirty) {
    voice_dirty=0u;NR32_REG=0u;
    if(music_song!=SONG_NONE) load_wave(mn_waves[mn_songs[music_song-1u][7]]);
  }
  if (fade_level != fade_target && ++fade_t >= 8u) {
    fade_t = 0;
    fade_level = fade_level < fade_target ? (uint8_t)(fade_level + 1u) : (uint8_t)(fade_level - 1u);
    NR50_REG = (uint8_t)((fade_level << 4) | fade_level);
  }
  if (music_song != SONG_NONE) {
    if (!--row_wait) {
      row_wait = fpr;
      for (c = 0; c != 4u; c++) channel_row(c);
    }
    if (echo_t && !--echo_t && echo_note != MN_REST)
      pulse(1, echo_note, scale_env((uint8_t)((lead_env & 0x0fu) | 0x30u), music_volume), lead_duty);
  }
  fx_tick(0);
  fx_tick(1);
}
