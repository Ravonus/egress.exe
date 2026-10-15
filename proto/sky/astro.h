/* EGRESS birth charts (proto): the heir's birth record, the chart, and its small effects. Portable C, on top of
 * sky.h. Every number here is a percent point in one of the matrix's stat groups (redesign 5.1), or a tag. */
#ifndef EG_ASTRO_H
#define EG_ASTRO_H
#include <stdint.h>
#include "sky.h"

/* placements a heir can align (the six the chart screen steps through, plus Jupiter and Saturn shown, not aligned) */
enum { AP_SUN, AP_MOON, AP_RISING, AP_MERCURY, AP_VENUS, AP_MARS, AP_COUNT };
/* elements: sign % 4 (Aries fire, Taurus earth, Gemini air, Cancer water, ...) -> the matrix's tags */
enum { AE_FIRE, AE_EARTH, AE_AIR, AE_WATER };
/* clock quality of a birth (2 bits); AQ_UNSET also marks a clock resumed from SRAM after power-off (stale) */
enum { AQ_UNSET, AQ_PLAYER, AQ_HOST, AQ_SEEDED };

/* The birth data, split the way the family tree's node record (redesign 9.6) holds it:
 * - the birth stamp, node offset 22, 5 bytes (the slot 9.6 reserved): u32 LE, bits 0-25 minutes since
 *   2000-01-01 00:00 UTC (to 2127), bits 26-27 clock quality (AQ_*), bits 28-29 birth order (0..2), bit 30 approx
 *   (outside 2026..2126), bit 31 spare; then 1 byte, the UTC offset in 15-minute steps (signed) at that moment;
 * - the sky bytes, node offset 44 (the node grows 44 -> 47): tiers 2 bits x 6 (Sun, Moon, Rising, Mercury, Venus,
 *   Mars), echo 3 bits (the parent's placement, 7 none), strengthened aspect 4 bits (pair, 15 none) + its tier 2 bits.
 * Code that only shows dates (tree, epitaph) reads stamp & 0x03FFFFFF. */
#define ASTRO_STAMP 5
#define ASTRO_SKY 3
typedef struct {
  uint16_t date;     /* UTC days since 2000-01-01 */
  uint16_t mod;      /* UTC minute of the day */
  int8_t utc_q;      /* the player's UTC offset then, in quarter hours (display only) */
  uint8_t quality;   /* AQ_* */
  uint8_t approx;    /* 1: born outside the baked tables (2026..2126) */
  uint8_t order;     /* 0..2: first, second, third of the siblings */
  uint8_t tier[AP_COUNT]; /* 0..2 each */
  uint8_t echo;      /* the parent's echo: placement 0..5, 7 none */
  uint8_t asp_pair, asp_tier; /* a strengthened aspect: pair index (0..6), 15 none; tier 0..2 */
} astro_birth;
void astro_pack(const astro_birth *b, uint8_t *stamp5, uint8_t *sky3);
void astro_unpack(const uint8_t *stamp5, const uint8_t *sky3, astro_birth *b);
#define ASTRO_TIER_CAP 3u /* all tiers + the aspect tier of one heir */
uint8_t astro_tiers_used(const astro_birth *b);

typedef struct { uint8_t a, b, kind; uint16_t miss; } astro_asp; /* chart point indexes (SKY_*), SKY_CONJ.. */
typedef struct {
  uint16_t pos[SKY_NPOS];
  uint8_t sign[SKY_NPOS];
  uint8_t nasp;
  astro_asp asp[24];
} astro_chart;
void astro_chart_make(uint16_t date, uint16_t mod, astro_chart *c);
#define astro_elem(sign) ((uint8_t)((sign) & 3u))

/* the combat-facing effects (the matrix sim and the game read the same struct) */
typedef struct {
  uint8_t aff_elem, aff_mag;   /* Sun: an on-hit status of its element ... */
  uint8_t opp_elem, opp16;     /* ... and its opposite element hurts a little more (x opp16 / 16) */
  uint8_t chg_elem, chg_mag;   /* Mars: the charge release carries its element */
  uint8_t kit_elem, kit_n;     /* Rising: what you start holding (run meta) */
  uint8_t mood;                /* Moon: idle and whispers (flavour) */
  int8_t price_pct;            /* Venus: vs a trader whose Sun shares Venus's element (economy) */
  uint8_t talk_extra;          /* Mercury: one talk in N gets an extra option (0 never) */
  int8_t g_dmg, g_def, g_avoid, g_spd, g_status, g_hp; /* aspect and transit nudges, percent points */
  uint8_t mix_pen, foe_pct, dark16, hot_pct, parry_mag; int8_t off; /* the sky today (session) */
  uint8_t dark_b16, mix_b16;   /* SKYBORN: dark-room and mix-room pressure, x / 16 (16 = none) */
} astro_fx;
void astro_fx_clear(astro_fx *f);
/* the natal part: placements, tiers, the echo and the chart's own aspects */
void astro_natal_fx(const astro_chart *c, const astro_birth *b, astro_fx *f);

/* transits: now's Sun..Saturn against the natal Sun, Moon, Mercury, Venus, Mars and Rising */
typedef struct { uint8_t body, natal, kind; uint16_t miss; } astro_hit;
uint8_t astro_transits(const astro_chart *natal, const uint16_t *now, astro_hit *out, uint8_t max);
/* the session: at most two transit nudges (the tightest), returns, and the sky states (sky_read) */
#define AS_MERC_RX 1u
#define AS_VENUS_RX 2u
#define AS_MARS_RX 4u
#define AS_JUP_STATION 8u
#define AS_SAT_STATION 16u
#define AS_ECL_SUN 32u
#define AS_ECL_MOON 64u
#define AS_FULL 128u
#define AS_NEW 256u
#define AS_SUPER 512u
#define AS_VOC 1024u
#define AS_SEASON 2048u
#define AS_SOLAR_RETURN 4096u
#define AS_LUNAR_RETURN 8192u
#define AS_METEOR 16384u /* a shower's window (24 h before its peak .. 12 h after); the caller sets it: astro_meteor_on */
uint16_t astro_states(const sky_now *n, const astro_chart *natal);
void astro_session_fx(uint16_t states, const astro_hit *hits, uint8_t nhits, astro_fx *f);

/* minutes since 2026-01-01: 1 while a meteor shower's window is open (proto/skyscenes meteor table) */
uint8_t astro_meteor_on(uint32_t m2026);

/* Event-born (redesign 9.7.11, 19.4). The genome's spare flag (9.2) is SKYBORN: set at birth when an event is active
 * at the birth minute, never chosen, never inherited. The kind is not stored: it is recomputed from the stamp. One
 * kind per heir, the rarest active event wins (19.1's order). In 2026..2126 only (an approx birth is never sky-born). */
enum { AB_NONE, AB_HALLEY, AB_ECLIPSE, AB_INGRESS, AB_RETRO, AB_MOON, AB_METEOR, AB_SEASON, AB_COUNT };
#define ASTRO_HALLEY_M 18708480ul /* 2061-07-28 00:00 UTC, minutes since 2026-01-01 (perihelion, about) */
uint8_t astro_born_kind(uint32_t m2026);
/* the small third trait of a sky-born heir (never one of the 2 expressed traits) */
void astro_skyborn_fx(uint8_t kind, astro_fx *f);

/* Siblings (redesign 9.7.2). The first-born takes the death minute; each later child is born just after the Rising
 * has crossed into the next sign (and at least 40 minutes after the last child), plus 7..37 seeded minutes, so the three Risings are always three signs (three
 * elements). Moon catch: when the Moon changes sign within 6 h after the second-born, the third-born waits for it,
 * so the Moons differ too. The slots on the pick screen hold the three in a seeded order (slot_order[slot] = birth
 * order). Times in minutes since 2000-01-01 UTC. Returns the number of sky_positions probes (about 84 ms each on
 * the cart; the work runs behind the death sequence). */
#ifndef ASTRO_SIB_MOON_WAIT
#define ASTRO_SIB_MOON_WAIT 360u
#endif
#define ASTRO_SIB_MIN_GAP 40u /* minutes: with the 7..37 offset, adjacent Risings sit at least ~8 deg apart */
uint8_t astro_siblings(uint32_t death_m2000, uint32_t seed, uint32_t *born_m2000, uint8_t *slot_order);

/* alignment: 0 allowed, else why not (1 at the cap, 2 tier 2 needs the sky, 3 the Moon is void of course, 4 maxed) */
uint8_t astro_can_align(const astro_birth *b, const astro_chart *natal, uint8_t placement, const sky_now *now);
/* a child's echo: the parent's most aligned placement (ties: Sun first), 7 when the parent aligned nothing */
uint8_t astro_echo_of(const astro_birth *parent);
#endif
