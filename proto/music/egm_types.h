/* EGM data types (the contract between compose.py and egm.c). */
#ifndef EGM_TYPES_H
#define EGM_TYPES_H
#include <stdint.h>

/* A pulse instrument. Envelopes use the hardware where they can: an attack is a rising hardware envelope from 0
 * (att = its pace, 0: start at peak), the hold is flat (dec = 0) or a falling pluck (dec = pace), the release at note
 * off is a falling envelope from the current level (rel = pace, 0: cut). vib: high nibble = delay in 4-frame units,
 * low nibble = 1 for a late vibrato of about +-14 cents. Peaks are 4-bit levels before the volume scaling. */
typedef struct {
  uint8_t duty, peak, att, dec, rel, vib;
} egm_inst_t;

/* Biome palette. inst[]: the instrument used for each pattern slot (PAD, ARP, LEAD, BELL). */
#define EGM_SLOT_PAD 0u
#define EGM_SLOT_ARP 1u
#define EGM_SLOT_LEAD 2u
#define EGM_SLOT_BELL 3u
#define EGM_BF_SWING 1u   /* 16ths alternate fpr+1 / fpr-1 */
#define EGM_BF_PANFLIP 2u /* the noise channel changes side by bar, from the seed */
typedef struct {
  uint8_t root, wave, inst[4], kit, pan, echo, flags, lead_up;
  int8_t motif[4]; /* key degrees: the biome's signature, answered at phrase ends */
} egm_biome_t;

/* Song (mood). base[]: pitch index of the key's C for pad, lead and bass (0 = C2 pulse; the wave sounds an octave
 * lower). mask[i]: the layers (bit c = channel c) heard at intensity i. hi_from[c]: the intensity from which layer c
 * uses its high pool. pool[c][tier]: count, then set ids. prog: nprog progressions of four chords (one a bar). */
#define EGM_SF_STUTTER 1u /* the last bar of a phrase may end in a soft noise roll */
#define EGM_SF_OCTAVE 2u  /* the seed may lift a phrase's lead an octave */
typedef struct {
  uint8_t fpr, mode, flags, bass_lvl, base[3];
  uint8_t mask[4], hi_from[4];
  uint8_t nform, nprog;
  const uint8_t *form;
  const uint8_t *prog;
  const uint8_t *pool[4][2];
  uint8_t motif_pat, lead_max;
} egm_song_t;

/* Effect: up to two parts (pulse 1 notes, noise hits). Pulse steps: frames, pitch byte (as in patterns, from the
 * effect's base octave), NRx2 envelope, duty. Noise steps: frames, NR43, NR42, 0. A zero frame count ends a part. */
typedef struct {
  uint8_t prio, base;
  const uint8_t *p1, *nz;
} egm_sfx_t;

/* Pattern bytes: two per event, rows rising, 0xFF ends.
 *   b0: row (bits 0-3) | length code (bits 4-6: 1 2 3 4 6 8 12 16 rows) | accent (bit 7)
 *   b1: kind (bits 6-7) | value (bits 0-5)
 *       kind 0: chord tone (bits 0-1: root, 3rd, 5th, 7th/9th; bits 2-5: octave)
 *       kind 1: scale degree relative to the chord root, value - 8
 *       kind 2: scale degree of the key, value - 8
 *       kind 3: 0 off; 2..5 the biome motif's notes
 *       noise: the kit sound 0..3
 * Chord byte: root degree (bits 0-2) | sus4 (bit 3) | sus2 (bit 4) | add9 (bit 5: tone 3 is the 9th). */
#define EGM_K_TONE 0x00u
#define EGM_K_REL 0x40u
#define EGM_K_KEY 0x80u
#define EGM_K_SPEC 0xc0u
#define EGM_CH_SUS4 0x08u
#define EGM_CH_SUS2 0x10u
#define EGM_CH_ADD9 0x20u
#endif
