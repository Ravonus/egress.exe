/* EGM: the prototype music + SFX driver (see egm.h and DESIGN.md).
 * Ideas taken from CRUCIBLE.EXE's cartridge driver (~/dev/crucible.exe/cartridge/src/crucible_sound.c, MIT, same
 * owner): the volume scaling of envelope start levels (m_k), the nearest-of-four wave level, effects holding a
 * channel the music then leaves alone, centred effects via NR51, and a random stream of the music's own (never
 * gameplay RNG; here a stride through a shuffled table, cheaper than xorshift on SDCC). The layered phrase stitching,
 * chord-relative patterns, per-channel gain ramps, two-cursor crossfade, decode-ahead rows and tonal effects are new. */
#include <gb/gb.h>
#include <gb/hardware.h>
#include "egm.h"
#include "egm_types.h"
#include "egm_data.h"

#define NONE 0xffu
#define V_OFF 0u
#define V_ATT 1u
#define V_SUS 2u
#define V_REL 3u
#define V_ECHO 4u
#define XF_ANSWER 1u
#define XF_THIN 2u
#define XF_HUSH 4u
#define XF_OCT 8u
#define G_FULL 16u
#define EGM_DUCK 4u /* of 16: about -2.5 dB on the music while an effect plays */

static const uint8_t len_rows[8] = {1, 2, 3, 4, 6, 8, 12, 16};

typedef struct {
  const egm_song_t *s;
  const egm_biome_t *b;
  uint8_t ri, rstep; /* the music's own random stream: an odd stride through egm_rand (cheap, deterministic) */
  uint8_t clock, row, bar, phrase, letter, xf, chord, stut, ev, seen, mood;
  const uint8_t *prog;
  const uint8_t *p[4];
  uint8_t slot[4];
  uint8_t ch[4][8]; /* per phrase letter: progression, a set choice per layer, transform (rows of 8: shifts) */
  const egm_inst_t *ip[4];
  int8_t rt;          /* the key, within a tritone of C */
  const uint8_t *md;  /* the mode's seven steps */
  /* the next row, decoded ahead (one step a frame) so the row's own frame only writes registers */
  uint8_t prep, hold, fresh;
  uint8_t nk[4], np[4], ng[4], na[4];
} cur_t;

static cur_t cur[2];
static cur_t *const cp[2] = {&cur[0], &cur[1]}; /* no multiply by sizeof(cur_t) per access */
static uint8_t m;                                  /* the main cursor; the other one plays tails */
static uint8_t own[4];                             /* the cursor driving each channel, NONE: silent */
static uint8_t gain[4], goal[4], grate[4], gt[4];  /* per-channel gain 0..16 and its ramp */
static uint8_t mv, mv_goal, mv_rate, mv_t;         /* music volume 0..64 (8 per settings step) */
static uint8_t svol;                               /* SFX volume 0..8 */
static uint8_t duck, duck_t;                       /* music gain taken off while an effect plays */
static uint8_t level_i, pend_int, pend_on, pend_mood, pend_biome, pend_mode;
static uint8_t master, master_goal, master_rate, master_t;
static uint8_t pan_noise, pan_cur, wave_id;
static uint16_t gseed;
static uint8_t cut_mask; /* layers the next downbeat cuts (EGM_DROP) */
uint8_t egm_row, egm_bar, egm_chord, egm_mood;

/* voices: channel 0 pulse 1, 1 pulse 2, 2 wave (3 noise is one-shot) */
static uint8_t vph[3], vt[3], vgate[3], vpeak[3], vlev[3], vage[3], vdage[2], vecho[2], vbl, vd[2], vdh[2], vdel[2];
static uint8_t kchg; /* the channel volumes moved this frame */
static uint16_t vf[3];
static const egm_inst_t *vin[2];

/* effects: 0 pulse 1, 1 noise. Steps: frames, NRx2, NR13 (noise: NR43), duty<<6 | NR14 bits */
static uint8_t fxbuf[2][32];
static const uint8_t *fx[2];
static uint8_t fx_left[2], fx_prio[2];

static uint8_t rnd(cur_t *u) {
  u->ri += u->rstep;
  return egm_rand[u->ri];
}

static uint8_t held(uint8_t c) { return c == 0u ? fx[0] != 0 : c == 3u ? fx[1] != 0 : 0u; }

/* kc[c]: the channel's volume 0..16 (gain x MUSIC volume, less the duck); recomputed only when something moved.
 * egm_lv[k][l] scales a 4-bit level by k/16 (a scaled non-zero level never rounds to 0 while k > 0). */
static uint8_t kc[4], kdirty, mvk[17], mvk_of = 0xffu;
static void k_update(void) {
  uint8_t c, g;
  kdirty = 0;
  if (mvk_of != mv) { /* gain -> volume for this MUSIC level: 17 multiplies only when the setting moves */
    mvk_of = mv;
    for (g = 0; g <= 16u; g++) mvk[g] = (uint8_t)(((uint16_t)g * mv) >> 6);
  }
  for (c = 0; c < 4u; c++) {
    g = gain[c];
    if (duck && !held(c)) g = g > duck ? (uint8_t)(g - duck) : 0u;
    kc[c] = mvk[g];
  }
  kchg = 1u;
}
#define LV(c, l) egm_lv[kc[c]][(l) > 15u ? 15u : (l)]

static void pw(uint8_t c, uint8_t env, uint16_t f, uint8_t duty) {
  if (!(env & 0xf8u)) {
    if (c) NR22_REG = 0; else NR12_REG = 0;
    return;
  }
  if (c == 0u) {
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

static void silence(uint8_t c) {
  if (c == 0u) NR12_REG = 0;
  else if (c == 1u) NR22_REG = 0;
  else if (c == 2u) NR32_REG = 0;
  else NR42_REG = 0;
}

/* ---- voices ---- */
static void strike(uint8_t c) {
  const egm_inst_t *in = vin[c];
  uint8_t l = LV(c, vpeak[c]);
  vlev[c] = l;
  vdage[c] = 0;
  vph[c] = V_SUS;
  if (!held(c)) pw(c, (uint8_t)((l << 4) | in->dec), vf[c], in->duty);
}

static void pulse_on(uint8_t c, uint8_t pitch, const egm_inst_t *in, uint8_t gate, uint8_t acc, uint8_t echo) {
  uint8_t l, p = (uint8_t)(in->peak + (acc ? 2u : 0u));
  if (p > 15u) p = 15u;
  /* legato: a held swell/flat note moving to the next chord tone changes pitch without a retrigger (no click,
   * no re-attack); only pads ask for it (their gates run to the next note) */
  if (vph[c] == V_SUS && vin[c] == in && in->att && !in->dec && vpeak[c] == p && !held(c)) {
    uint16_t f = egm_period[pitch];
    vf[c] = f;
    vgate[c] = gate;
    vd[c] = (uint8_t)((2048u - f) >> 7);
    if (!vd[c]) vd[c] = 1u;
    vdh[c] = (uint8_t)((vd[c] + 1u) >> 1);
    if (c == 0u) {
      NR13_REG = (uint8_t)f;
      NR14_REG = (uint8_t)(f >> 8);
    } else {
      NR23_REG = (uint8_t)f;
      NR24_REG = (uint8_t)(f >> 8);
    }
    return;
  }
  vin[c] = in;
  vf[c] = egm_period[pitch];
  vpeak[c] = p;
  vgate[c] = gate;
  vage[c] = 0;
  vecho[c] = echo;
  vdel[c] = (in->vib & 15u) ? (uint8_t)((in->vib >> 4) << 2) : 255u;
  vd[c] = (uint8_t)((2048u - vf[c]) >> 7); /* about 14 cents at any pitch */
  if (!vd[c]) vd[c] = 1u;
  vdh[c] = (uint8_t)((vd[c] + 1u) >> 1);
  l = LV(c, vpeak[c]);
  if (in->att && l) {
    vph[c] = V_ATT;
    vt[c] = (uint8_t)(((uint16_t)l * in->att * 15u) >> 4);
    if (!vt[c]) vt[c] = 1u;
    vlev[c] = l;
    if (!held(c)) pw(c, (uint8_t)(0x08u | in->att), vf[c], in->duty);
  } else
    strike(c);
}

static void release(uint8_t c) {
  const egm_inst_t *in = vin[c];
  uint8_t l = vlev[c], d;
  if (vph[c] == V_ATT) {
    d = (uint8_t)(((uint16_t)vage[c] << 4) / (uint8_t)(15u * in->att));
    if (d < l) l = d;
  } else if (in->dec) {
    d = (uint8_t)(((uint16_t)vdage[c] << 4) / (uint8_t)(15u * in->dec));
    l = l > d ? (uint8_t)(l - d) : 0u;
  }
  vph[c] = V_REL;
  if (c == 1u && vecho[1]) {
    vph[1] = V_ECHO;
    vt[1] = vecho[1];
  }
  if (held(c)) return;
  if (l && in->rel) pw(c, (uint8_t)((l << 4) | in->rel), vf[c], in->duty);
  else silence(c);
}

static uint8_t wcode(void) {
  uint8_t s = (uint8_t)(kc[2] >> (uint8_t)(vbl - 1u));
  return s >= 12u ? 0x20u : s >= 6u ? 0x40u : kc[2] ? 0x60u : 0u; /* the bass never vanishes before the pulses */
}

static void wave_on(uint8_t pitch, uint8_t gate, uint8_t lvl) {
  uint16_t f = egm_period[pitch];
  uint8_t w;
  vbl = lvl;
  vgate[2] = gate;
  w = wcode();
  if (vph[2] == V_SUS && w && w == vlev[2] && f != vf[2]) { /* legato: a new pitch on the held bass, no retrigger */
    vf[2] = f;
    NR33_REG = (uint8_t)f;
    NR34_REG = (uint8_t)(f >> 8);
    return;
  }
  vf[2] = f;
  vph[2] = V_SUS;
  vlev[2] = w;
  if (!w) {
    NR32_REG = 0;
    return;
  }
  NR30_REG = 0x80u;
  NR32_REG = w;
  NR33_REG = (uint8_t)f;
  NR34_REG = (uint8_t)(0x80u | (f >> 8));
}

static void wave_release(uint8_t fast) {
  vph[2] = V_REL;
  vt[2] = fast ? 1u : 3u;
}

static void noise_on(const egm_biome_t *b, uint8_t snd, uint8_t acc, uint8_t soft) {
  const uint8_t *k = egm_kits[b->kit][snd & 3u]; /* 8-byte rows: shifts, no multiply */
  uint8_t l = (uint8_t)((k[0] >> 4) + (acc ? 2u : 0u));
  if (soft && l > soft) l = (uint8_t)(l - soft);
  l = LV(3, l);
  if (held(3) || !l) return;
  NR41_REG = 0;
  NR42_REG = (uint8_t)((l << 4) | (k[0] & 15u));
  NR43_REG = k[1];
  NR44_REG = 0x80u;
}

static void voice_tick(uint8_t c) {
  const egm_inst_t *in = vin[c];
  uint8_t l;
  if (vph[c] == V_OFF || vph[c] == V_REL) return;
  if (vage[c] != 255u) vage[c]++;
  if (vph[c] == V_ECHO) {
    if (--vt[c]) return;
    vph[c] = V_REL;
    l = LV(c, (uint8_t)((vpeak[c] + 1u) >> 1));
    if (!held(c) && l) pw(c, (uint8_t)((l << 4) | 3u), vf[c], in->duty);
    return;
  }
  if (vgate[c] && !--vgate[c]) {
    release(c);
    return;
  }
  if (vph[c] == V_ATT) {
    if (!--vt[c]) strike(c);
    return;
  }
  /* V_SUS */
  if (vdage[c] != 255u) vdage[c]++;
  if (kchg && !in->dec && !held(c)) {
    l = LV(c, vpeak[c]);
    if (l != vlev[c]) strike(c); /* a fade or a duck moved this held note's level */
  }
  if (vage[c] > vdel[c] && !(vage[c] & 1u) && !held(c)) {
    /* a late triangle vibrato, 8 steps of 2 frames: 0 +h +d +h 0 -h -d -h */
    uint8_t ph = (uint8_t)((vage[c] >> 1) & 7u);
    uint16_t f = vf[c];
    if (ph == 1u || ph == 3u) f += vdh[c];
    else if (ph == 2u) f += vd[c];
    else if (ph == 5u || ph == 7u) f -= vdh[c];
    else if (ph == 6u) f -= vd[c];
    if (c == 0u) {
      NR13_REG = (uint8_t)f;
      NR14_REG = (uint8_t)(f >> 8);
    } else {
      NR23_REG = (uint8_t)f;
      NR24_REG = (uint8_t)(f >> 8);
    }
  }
}

static void wave_tick(void) {
  uint8_t w;
  if (vph[2] == V_SUS) {
    if (vgate[2] && !--vgate[2]) {
      wave_release(0);
      return;
    }
    if (!kchg) return;
    w = wcode();
    if (w != vlev[2]) {
      vlev[2] = w;
      NR32_REG = w;
    }
  } else if (vph[2] == V_REL) {
    if (--vt[2]) return;
    vt[2] = 3u;
    w = vlev[2];
    w = (!w || w >= 0x60u) ? 0u : (uint8_t)(w + 0x20u); /* 100 -> 50 -> 25 % -> off: a soft tail, not a cut */
    vlev[2] = w;
    NR32_REG = w;
    if (!w) vph[2] = V_OFF;
  }
}

/* ---- pitch ---- */
static uint8_t deg_pitch(const cur_t *u, int8_t deg, uint8_t base) {
  uint8_t d = (uint8_t)(deg + 21) & 63u; /* tables, no division: octave*12 and the step within the octave */
  int16_t p = (int16_t)base + u->rt + egm_oct12[d] + u->md[egm_mod7[d]] - 36;
  if (p < 0) p += 12;
  if (p > 71) p = 71;
  return (uint8_t)p;
}

static int8_t tone_deg(uint8_t chord, uint8_t t) {
  uint8_t i = t & 3u;
  int8_t d = i == 0u ? 0 : i == 1u ? ((chord & EGM_CH_SUS4) ? 3 : (chord & EGM_CH_SUS2) ? 1 : 2) : i == 2u ? 4
                                                                                   : ((chord & EGM_CH_ADD9) ? 8 : 6);
  return (int8_t)((chord & 7u) + d + 7 * (t >> 2));
}

/* a held note lands on a chord tone: never a second or fourth against the bar's chord */
static int8_t snap(uint8_t chord, int8_t deg) {
  uint8_t r = egm_mod7[(uint8_t)(deg - (int8_t)(chord & 7u) + 21) & 63u];
  if (r == 2u && (chord & EGM_CH_SUS4)) return (int8_t)(deg + 1);
  if (r == 2u && (chord & EGM_CH_SUS2)) return (int8_t)(deg - 1);
  if ((r == 1u && !(chord & EGM_CH_SUS2)) || (r == 3u && !(chord & EGM_CH_SUS4)) || r == 5u) return (int8_t)(deg - 1);
  return deg;
}

static uint8_t row_frames(const cur_t *u) {
  uint8_t f = u->s->fpr;
  if (u->b->flags & EGM_BF_SWING) f = (u->row & 1u) ? (uint8_t)(f - 1u) : (uint8_t)(f + 1u);
  return f;
}

#define NK_NONE 0u
#define NK_NOTE 1u
#define NK_OFF 2u
/* decode channel c's event on the row about to fire (u->row) into nk/np/ng/na */
static void resolve(cur_t *u, uint8_t c) {
  const uint8_t *e = u->p[c];
  uint8_t b0, b1, len, kind, v, base, pitch;
  int8_t deg;
  u->nk[c] = NK_NONE;
  if (!e || e[0] == 0xffu || (e[0] & 15u) != u->row) return;
  u->p[c] = e + 2;
  if (c == 1u) {
    v = u->ev++;
    if ((u->xf & XF_THIN) && (u->bar & 1u) && (v & 1u)) return;
  }
  b0 = e[0];
  b1 = e[1];
  len = len_rows[(b0 >> 4) & 7u];
  kind = b1 & 0xc0u;
  v = b1 & 0x3fu;
  u->na[c] = b0 & 0x80u;
  u->nk[c] = NK_NOTE;
  if (c == 3u) {
    u->np[3] = v;
    return;
  }
  if (kind == EGM_K_SPEC && v == 0u) {
    u->nk[c] = NK_OFF;
    return;
  }
  if (kind == EGM_K_TONE)
    deg = tone_deg(u->chord, v);
  else {
    deg = kind == EGM_K_SPEC ? u->b->motif[(v - 2u) & 3u]
          : kind == EGM_K_REL ? (int8_t)((u->chord & 7u) + v - 8u)
                              : (int8_t)(v - 8u);
    if (len >= 6u) deg = snap(u->chord, deg);
  }
  base = u->s->base[c];
  if (c == 1u) {
    base = (uint8_t)(base + u->b->lead_up);
    if (u->xf & XF_OCT) base += 12u;
  }
  pitch = deg_pitch(u, deg, base);
  if (c == 1u)
    while (pitch > u->s->lead_max) pitch -= 12u;
  u->np[c] = pitch;
  /* long pad and bass notes hold to the next note (legato); short ones and the lead breathe 2 frames first */
  u->ng[c] = (uint8_t)(len * u->s->fpr - ((len >= 4u && (c == 2u || (c == 0u && u->slot[0] == EGM_SLOT_PAD))) ? 0u : 2u));
}

/* ---- form: 4-bar phrases from the song's letters, the choices re-rolled every cycle ---- */
/* a letter's choices, rolled the first time it plays in a cycle (one letter at a time keeps the frame cheap) */
static void roll(cur_t *u, uint8_t L) {
  uint8_t i, r, *ch = u->ch[L];
  for (i = 0; i < 5u; i++) ch[i] = rnd(u);
  r = rnd(u);
  ch[5] = (uint8_t)(((r & 1u) ? XF_ANSWER : 0u) | (((r >> 1) & 3u) == 0u ? XF_THIN : 0u) |
                    (((r >> 3) & 7u) == 0u ? XF_HUSH : 0u) |
                    ((u->s->flags & EGM_SF_OCTAVE) && ((r >> 6) & 3u) == 0u ? XF_OCT : 0u));
}

static void phrase_start(cur_t *u) {
  uint8_t L;
  if (u->phrase >= u->s->nform) {
    u->phrase = 0;
    u->seen = 0;
  }
  L = u->s->form[u->phrase];
  u->letter = L;
  if (!(u->seen & (uint8_t)(1u << L))) roll(u, L);
  u->prog = u->s->prog + ((u->ch[L][0] % u->s->nprog) << 2);
  u->xf = u->ch[L][5];
  if (u->seen & (uint8_t)(1u << L)) u->xf ^= (uint8_t)(XF_ANSWER | XF_THIN); /* A' answers where A did not */
  u->seen |= (uint8_t)(1u << L);
}

static void bar_start(uint8_t x) {
  cur_t *u = cp[x];
  const uint8_t *pool, *set;
  uint8_t c, tier, pat;
  if (u->bar == 0u) phrase_start(u);
  u->chord = u->prog[u->bar];
  for (c = 0; c < 4u; c++) {
    u->p[c] = 0;
    tier = (uint8_t)(level_i >= u->s->hi_from[c]);
    pool = u->s->pool[c][tier];
    if (!pool) pool = u->s->pool[c][0];
    if (!pool || !pool[0]) continue;
    set = egm_sets[pool[1u + u->ch[u->letter][1u + c] % pool[0]]];
    u->slot[c] = set[0];
    pat = set[1u + u->bar];
    if (c == 1u) {
      if (u->bar == 3u && (u->xf & XF_ANSWER)) pat = u->s->motif_pat;
      if (u->bar == 2u && (u->xf & XF_HUSH)) pat = NONE;
    }
    if (pat != NONE) u->p[c] = egm_pat + egm_pat_at[pat];
  }
  u->stut = (uint8_t)((u->s->flags & EGM_SF_STUTTER) && u->bar == 3u && rnd(u) < 96u);
  if (u->b->flags & EGM_BF_PANFLIP) pan_noise = (rnd(u) & 1u) ? 0x80u : 0x08u;
  u->ev = 0;
}

static void load_wave(uint8_t w) {
  uint8_t i;
  const uint8_t *p = egm_waves[w];
  NR30_REG = 0;
  for (i = 0; i < 16u; i++) (&AUD3WAVE[0])[i] = p[i];
  NR30_REG = 0x80u;
  wave_id = w;
}

static void cur_init(uint8_t x, uint8_t mood, uint8_t biome, uint16_t seed) {
  cur_t *u = cp[x];
  u->s = &egm_songs[mood];
  u->b = &egm_biomes[biome];
  u->mood = mood;
  {
    uint8_t i;
    u->md = egm_modes[u->s->mode];
    for (i = 0; i < 4u; i++) u->ip[i] = &egm_insts[u->b->inst[i]];
    u->rt = u->b->root > 6u ? (int8_t)(u->b->root - 12u) : (int8_t)u->b->root;
  }
  u->ri = (uint8_t)seed;
  u->rstep = (uint8_t)((seed >> 8) | 1u);
  u->row = u->bar = 0;
  u->phrase = 0xf0u; /* rolls on the first phrase */
  u->clock = 0;
  u->hold = 0;
  u->prep = 0;
  u->fresh = 1u; /* its first preparation waits a frame: a song change's frame is already full */
  pan_noise = 0x88u;
}

/* layers in (from silence, over a bar) and out (to silence, over a bar) */
static void set_mask(uint8_t x, uint8_t mask, uint8_t rate) {
  uint8_t c;
  for (c = 0; c < 4u; c++) {
    if (mask & (uint8_t)(1u << c)) {
      if (own[c] != x) {
        own[c] = x;
        gain[c] = 0;
      }
      goal[c] = G_FULL;
    } else if (own[c] == x)
      goal[c] = 0;
    else
      continue;
    grate[c] = rate;
    gt[c] = rate;
  }
  k_update();
}

static uint8_t remaining(const cur_t *u) {
  uint8_t f = row_frames(u);
  return (uint8_t)(u->hold + (f > u->clock ? f - u->clock : 0u));
}

/* the main cursor's bar line: pending song changes and intensity land here */
static void bar_line(void) {
  uint8_t o, c, mask, nf;
  if (pend_on) {
    pend_on = 0;
    o = m;
    m ^= 1u;
    cur_init(m, pend_mood, pend_biome, (uint16_t)(gseed += 0x9e37u));
    egm_mood = pend_mood;
    nf = cp[m]->s->fpr;
    mask = cp[m]->s->mask[level_i];
    if (cp[m]->b->wave != wave_id) {
      vph[2] = V_OFF;
      load_wave(cp[m]->b->wave);
    }
    for (c = 0; c < 4u; c++) {
      if (mask & (uint8_t)(1u << c)) {
        if (own[c] == NONE) gain[c] = pend_mode == EGM_XFADE ? 0u : 10u;
        else gain[c] = pend_mode == EGM_XFADE ? 8u : 14u;
        own[c] = m;
        goal[c] = G_FULL;
        grate[c] = gt[c] = pend_mode == EGM_XFADE ? nf : 2u;
      } else if (own[c] == o) {
        if (pend_mode == EGM_XFADE) {
          goal[c] = 0;
          grate[c] = gt[c] = cp[o]->s->fpr;
        } else { /* the drop: the layer rings to the bar line and is cut on the new song's downbeat */
          own[c] = NONE;
          goal[c] = gain[c]; /* held at its level until the cut */
          cut_mask |= (uint8_t)(1u << c);
        }
      }
    }
    k_update();
    /* the new song's downbeat lands on the old song's bar line */
    {
      uint8_t r = remaining(cp[o]), f = row_frames(cp[m]);
      cp[m]->clock = r < f ? (uint8_t)(f - r) : 0u;
      cp[m]->hold = r > f ? (uint8_t)(r - f) : 0u;
    }
    return;
  }
  if (pend_int != NONE) {
    level_i = pend_int;
    pend_int = NONE;
    set_mask(m, cp[m]->s->mask[level_i], cp[m]->s->fpr);
  }
}

/* one preparation step: 0 the bar line (song changes, intensity, the bar's chord and patterns), 1..4 a channel */
static void prep_step(uint8_t x) {
  cur_t *u = cp[x];
  if (u->prep == 0u) {
    if (u->row == 0u) {
      if (x == m) bar_line();
      bar_start(x);
    } else if (x == m && pend_on && pend_mode == EGM_BEAT && !(u->row & 3u))
      bar_line(); /* a beat-quantised change */
  } else
    resolve(u, (uint8_t)(u->prep - 1u));
  u->prep++;
}

/* the row's frame: only register writes */
static void fire(uint8_t x) {
  cur_t *u = cp[x];
  uint8_t c;
  while (u->prep < 5u) prep_step(x);
  if (cut_mask && x == m) {
    for (c = 0; c < 4u; c++) {
      if (!(cut_mask & (uint8_t)(1u << c)) || own[c] != NONE) continue;
      gain[c] = goal[c] = 0;
      if (c == 2u) wave_release(1);
      else if (c < 2u && vph[c] != V_OFF) {
        release(c);
        vph[c] = V_OFF;
      } else
        silence(c);
    }
    cut_mask = 0;
    k_update();
  }
  for (c = 0; c < 4u; c++) {
    if (!u->nk[c] || own[c] != x) continue;
    if (u->nk[c] == NK_OFF) {
      if (c == 2u) {
        if (vph[2] == V_SUS) wave_release(0);
      } else if (vph[c] == V_ATT || vph[c] == V_SUS)
        release(c);
    } else if (c == 3u)
      noise_on(u->b, u->np[3], u->na[3], 0);
    else if (c == 2u)
      wave_on(u->np[2], u->ng[2], u->s->bass_lvl);
    else
      pulse_on(c, u->np[c], u->ip[u->slot[c]], u->ng[c], u->na[c], c == 1u ? (uint8_t)(u->b->echo * u->s->fpr) : 0u);
  }
  if (u->stut && u->row >= 12u && own[3] == x) noise_on(u->b, 0, 0, (uint8_t)((u->row - 12u) >> 1));
  if (x == m) {
    egm_row = u->row;
    egm_bar = u->bar;
    egm_chord = u->chord;
  }
  u->prep = 0;
  if (++u->row >= 16u) {
    u->row = 0;
    if (++u->bar >= 4u) {
      u->bar = 0;
      u->phrase++;
    }
  }
}

static void cur_tick(uint8_t x) {
  cur_t *u = cp[x];
  uint8_t c;
  if (!u->s) return;
  if (x != m) {
    for (c = 0; c < 4u && own[c] != x; c++) {}
    if (c == 4u) {
      u->s = 0;
      return;
    }
  }
  if (u->hold) {
    u->hold--;
  } else if (++u->clock >= row_frames(u)) {
    u->clock = 0;
    fire(x);
    return;
  }
  if (u->fresh) u->fresh = 0;
  else if (u->prep < 5u) prep_step(x);
}

/* ---- effects ---- */
static void fx_start(uint8_t i) {
  const uint8_t *n = fx[i];
  if (!n[0]) {
    fx[i] = 0;
    k_update();
    if (i == 0u) { /* hand pulse 1 back: a held music note comes back at its level */
      if (vph[0] == V_SUS || vph[0] == V_ATT) strike(0);
      else NR12_REG = 0;
    } else
      NR42_REG = 0;
    return;
  }
  fx_left[i] = n[0];
  if (i == 0u) {
    NR10_REG = 0;
    NR11_REG = n[3] & 0xc0u;
    NR12_REG = n[1];
    NR13_REG = n[2];
    NR14_REG = (uint8_t)(0x80u | (n[3] & 7u));
  } else {
    NR41_REG = 0;
    NR42_REG = n[1];
    NR43_REG = n[2];
    NR44_REG = 0x80u;
  }
}

static uint8_t sfx_env(uint8_t e) {
  uint8_t v = (uint8_t)(((e >> 4) * svol + 4u) >> 3);
  if (!v && (e >> 4)) v = 1u;
  return (uint8_t)((v << 4) | (e & 15u));
}

void egm_sfx(uint8_t id) {
  const egm_sfx_t *s = &egm_sfxs[id];
  const cur_t *u = cp[m];
  const uint8_t *n;
  uint8_t *o, i, chord, kind, v, pitch;
  int8_t deg;
  uint16_t f;
  if (!svol || !u->s) return;
  if (s->p1 && (!fx[0] || fx_prio[0] <= s->prio)) {
    chord = u->chord;
    o = fxbuf[0];
    for (n = s->p1, i = 0; n[0] && i < 7u; n += 4, i++) {
      kind = n[1] & 0xc0u;
      v = n[1] & 0x3fu;
      /* tonal: from the key and the chord playing now */
      deg = kind == EGM_K_TONE ? tone_deg(chord, v) : kind == EGM_K_REL ? (int8_t)((chord & 7u) + v - 8u) : (int8_t)(v - 8u);
      pitch = deg_pitch(u, deg, s->base);
      f = egm_period[pitch];
      *o++ = n[0];
      *o++ = sfx_env(n[2]);
      *o++ = (uint8_t)f;
      *o++ = (uint8_t)((n[3] << 6) | (f >> 8));
    }
    *o = 0;
    fx[0] = fxbuf[0];
    fx_prio[0] = s->prio;
    fx_start(0);
  }
  if (s->nz && (!fx[1] || fx_prio[1] <= s->prio)) {
    o = fxbuf[1];
    for (n = s->nz, i = 0; n[0] && i < 7u; n += 4, i++) {
      *o++ = n[0];
      *o++ = sfx_env(n[2]);
      *o++ = n[1];
      *o++ = 0;
    }
    *o = 0;
    fx[1] = fxbuf[1];
    fx_prio[1] = s->prio;
    fx_start(1);
  }
  duck = EGM_DUCK;
  duck_t = 0;
  k_update();
}

static void fx_tick(void) {
  uint8_t i;
  for (i = 0; i < 2u; i++) {
    if (!fx[i]) continue;
    if (--fx_left[i]) continue;
    fx[i] += 4;
    fx_start(i);
  }
}

/* ---- API ---- */
void egm_init(void) {
  uint8_t c;
  NR52_REG = 0x80u;
  NR50_REG = 0x77u;
  NR51_REG = 0xffu;
  for (c = 0; c < 4u; c++) {
    own[c] = NONE;
    gain[c] = goal[c] = 0;
    silence(c);
  }
  vph[0] = vph[1] = vph[2] = V_OFF;
  cp[0]->s = cp[1]->s = 0;
  fx[0] = fx[1] = 0;
  m = 0;
  mv = mv_goal = 48u; /* MUSIC 6 of 8 */
  kdirty = 1u;
  svol = 7u;
  master = master_goal = 7u;
  level_i = 1u;
  pend_on = 0;
  pend_int = NONE;
  pan_cur = 0xffu;
  wave_id = NONE;
  gseed = 0x2d6bu;
}

void egm_play(uint8_t mood, uint8_t biome, uint16_t seed) {
  uint8_t c;
  for (c = 0; c < 4u; c++) {
    own[c] = NONE;
    silence(c);
  }
  cp[m ^ 1u]->s = 0;
  gseed = seed;
  cur_init(m, mood, biome, seed);
  egm_mood = mood;
  if (cp[m]->b->wave != wave_id) load_wave(cp[m]->b->wave);
  set_mask(m, cp[m]->s->mask[level_i], 4u);
  cp[m]->clock = 0; /* the first row is decoded over the next frames, then fires */
}

void egm_go(uint8_t mood, uint8_t biome, uint8_t mode) {
  uint8_t c, left, r;
  if (!cp[m]->s) {
    egm_play(mood, biome, gseed);
    return;
  }
  pend_on = 1u;
  pend_mood = mood;
  pend_biome = biome;
  pend_mode = mode;
  if (mode != EGM_XFADE) return;
  /* the old song leans back before the bar line so the new one can come in over it */
  left = (uint8_t)(16u - cp[m]->row);
  r = (uint8_t)((uint16_t)left * cp[m]->s->fpr / 6u);
  for (c = 0; c < 4u; c++)
    if (own[c] == m) {
      goal[c] = 10u;
      grate[c] = gt[c] = r ? r : 1u;
    }
}

void egm_intensity(uint8_t level) { pend_int = level > 3u ? 3u : level; }

void egm_stop(void) {
  uint8_t c;
  for (c = 0; c < 4u; c++)
    if (own[c] != NONE) {
      goal[c] = 0;
      grate[c] = gt[c] = 4u;
    }
}

void egm_music_volume(uint8_t v, uint8_t rate) {
  mv_goal = (uint8_t)((v > 8u ? 8u : v) << 3);
  mv_rate = mv_t = rate;
  if (!rate) mv = mv_goal;
  kdirty = 1u;
}

void egm_sfx_volume(uint8_t v) { svol = v > 8u ? 8u : v; }

void egm_master(uint8_t level, uint8_t rate) {
  master_goal = level > 7u ? 7u : level;
  master_rate = master_t = rate ? rate : 1u;
}

void egm_tick(void) {
  uint8_t c, p;
  /* ramps */
  if (master != master_goal && !--master_t) {
    master_t = master_rate;
    master = master < master_goal ? (uint8_t)(master + 1u) : (uint8_t)(master - 1u);
    NR50_REG = (uint8_t)(master * 0x11u);
  }
  if (mv != mv_goal && (!mv_t || !--mv_t)) {
    mv_t = mv_rate;
    mv = mv < mv_goal ? (uint8_t)(mv + 1u) : (uint8_t)(mv - 1u);
    kdirty = 1u;
  }
  for (c = 0; c < 4u; c++) {
    if (gain[c] == goal[c] || --gt[c]) continue;
    gt[c] = grate[c];
    kdirty = 1u;
    if (gain[c] < goal[c]) gain[c]++;
    else if (!--gain[c] && own[c] != NONE) {
      own[c] = NONE;
      if (c == 2u) wave_release(1);
      else if (c < 2u && vph[c] != V_OFF) {
        release(c);
        vph[c] = V_OFF; /* no echo after a fade */
      }
    }
  }
  if (duck && !fx[0] && !fx[1] && ++duck_t >= 6u) {
    duck_t = 0;
    duck--;
    kdirty = 1u;
  }
  fx_tick();
  if (kdirty) k_update();
  voice_tick(0);
  voice_tick(1);
  wave_tick();
  c = m; /* a song change inside cur_tick flips m: tick each cursor once */
  cur_tick(c);
  cur_tick(c ^ 1u);
  p = cp[m]->s ? cp[m]->b->pan : 0xffu;
  p = (uint8_t)((p & 0x77u) | (p & pan_noise));
  if (fx[0]) p |= 0x11u;
  if (fx[1]) p |= 0x88u;
  if (p != pan_cur) {
    pan_cur = p;
    NR51_REG = p;
  }
  kchg = 0;
}
