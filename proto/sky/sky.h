/* EGRESS sky engine (proto): live integer positions + baked event tables. Portable C (SDCC 4.5 / host cc).
 *
 * Time base: whole minutes, UTC. `date` = days since 2000-01-01, `mod` = minute of the day (0..1439).
 * Angles: uint16_t binary angles, 65536 = 360 degrees (0.0055 deg a step).
 * The tables cover 2026-01-01 00:00 .. 2127-01-01 00:00 UTC; outside, everything falls back to the live model and
 * SKY_APPROX is set. Nothing here ever fails: any date 2000..2179 gives a reading. */
#ifndef EG_SKY_H
#define EG_SKY_H
#include <stdint.h>
#include "sky_gen.h"



/* bodies (live) */
enum { SKY_SUN, SKY_MOON, SKY_MERCURY, SKY_VENUS, SKY_MARS, SKY_JUPITER, SKY_SATURN, SKY_NODE, SKY_ASC, SKY_MC, SKY_NPOS };
#define SKY_NPLANET 7 /* Sun .. Saturn */

typedef struct { uint8_t a1; int8_t k1; uint8_t a2; int8_t k2; uint8_t a3; int8_t k3; uint16_t ph; int16_t amp; } sky_term;
typedef struct { uint8_t out; uint8_t t0, nl, tr, nr; int16_t r0; } sky_body; /* r0: Q11 AU */
typedef struct {
  uint8_t bank, width, flag, kind, state0;
  uint16_t n;
  uint32_t bias;
  const uint8_t *idx, *dat;
} sky_stream;

extern const int16_t sky_sinq[2049];
extern const uint16_t sky_atan[1025];
extern const uint32_t sky_ang0[], sky_angd[], sky_angm[];
extern const sky_term sky_terms[];
extern const sky_body sky_bodies[];
extern const sky_stream sky_streams[SKY_NSTREAM];
extern const uint8_t sky_marks_bank;
extern const uint16_t sky_nmarks;
extern const uint8_t *const sky_marks_p;

int16_t sky_sin(uint16_t a);  /* Q14, interpolated: smooth for slow arguments (helio -> geo, the Ascendant) */
int16_t sky_sinf(uint16_t a); /* Q14, one lookup (8192 steps a turn): the series terms */
/* the event banks are read through a bank-0 copy (the engine itself sits in one switchable bank) */
void sky_copy(uint8_t bank, const uint8_t *src, uint8_t n, uint8_t *dst);
#ifdef __SDCC
int32_t sky_mul16(int16_t a, int16_t b);    /* sky_mul.s: 16 x 16 -> 32, about 5x faster than __mullong */
uint32_t sky_mulu16(uint16_t a, uint16_t b);
#else
#define sky_mul16(a, b) ((int32_t)(int16_t)(a) * (int16_t)(b))
#define sky_mulu16(a, b) ((uint32_t)(uint16_t)(a) * (uint16_t)(b))
#endif
#define sky_cos(a) sky_sin((uint16_t)((a) + 0x4000u))
uint16_t sky_atan2(int16_t y, int16_t x);

/* all live positions at a moment: Sun..Saturn geocentric apparent longitudes, the mean node, Ascendant and MC for
 * the shared world's latitude (40 N) on its meridian (0). */
void sky_positions(uint16_t date, uint16_t mod, uint16_t *out);

/* ---- baked events ---- */
#define SKY_RANGE_END 53121600ul /* minutes from 2026-01-01 to 2127-01-01 */
#define SKY_NO_EVENT 0xFFFFFFFFul
typedef struct { uint32_t at; uint16_t i; uint8_t state, flag; } sky_ev;
/* m: minutes since 2026-01-01 (in range). prev = the last event at or before m (at = SKY_NO_EVENT if none, state
 * = the state before the first event), next = the first event after m (at = SKY_NO_EVENT if none). */
void sky_find(uint8_t s, uint32_t m, sky_ev *prev, sky_ev *next);
uint8_t sky_mark(uint16_t phase_index, int8_t *off); /* flags of a new/full moon: bits 0-2 eclipse kind, bit 3 super */
#define SKY_ECL_KIND(f) ((f) & 7u) /* 1 partial sun, 2 annular, 3 total sun, 5 penumbral moon, 6 partial, 7 total */
#define SKY_SUPER 8u

/* ---- the sky now ---- */
#define SKY_APPROX 1u  /* outside the tables: the live model alone */
#define SKY_VOC 2u     /* the Moon is void of course */
#define SKY_ECLIPSE 4u /* an eclipse today (within 12 h) */
#define SKY_SUPERM 8u  /* a super new or full moon today */
typedef struct {
  uint8_t flags;
  uint8_t phase8;     /* 0 new .. 4 full .. 7 */
  uint8_t eclipse;    /* SKY_ECL_KIND of today's eclipse, else 0 */
  uint8_t retro;      /* bit b: planet b (SKY_MERCURY.. SKY_SATURN) is retrograde */
  uint8_t station;    /* bit b: planet b stations within 24 h (either way) */
  uint8_t moon_sign;
  uint32_t voc_end;   /* minute (since 2026) the Moon leaves its sign; valid when SKY_VOC */
  uint16_t pos[SKY_NPOS];
} sky_now;
/* m: minutes since 2026-01-01 (may be out of range: negative times are clamped to the live model) */
void sky_read(uint16_t date, uint16_t mod, sky_now *out);

/* ---- charts ---- */
enum { SKY_CONJ, SKY_SQUARE, SKY_TRINE, SKY_OPP, SKY_NASPECT };
/* the major aspect between two longitudes within orb (binary angle units), else 255; *exact = the miss */
uint8_t sky_aspect(uint16_t a, uint16_t b, uint16_t orb, uint16_t *miss);
#define SKY_DEG(d) ((uint16_t)((d) * 182u + (d) / 25u)) /* whole degrees -> binary angle (182.04) */

/* minutes since 2026-01-01 from a date/mod (may be negative: before 2026) */
int32_t sky_minutes(uint16_t date, uint16_t mod);
#endif
