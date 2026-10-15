/* EGM: EGRESS's prototype music + SFX driver (proto/music, not wired into the game yet).
 *
 * One system for music and effects. A song (a mood) is authored motifs, chord progressions and grooves; the seed
 * picks variations and stitches 4-bar phrases; the biome supplies the key, the timbres, the wave, the drum kit and a
 * signature motif; game state adds or removes layers. Transitions land on bar lines and crossfade per channel.
 * Tonal effects are pitched from the current key and chord, borrow pulse 1 or noise by priority, and duck the rest.
 *
 * Channel = layer: pulse 1 harmony (pad or arpeggio), pulse 2 lead (+ echo), wave bass, noise drums.
 * Call egm_tick() once per frame (VBlank). Data: egm_data.h (written by compose.py). */
#ifndef EGM_H
#define EGM_H
#include <stdint.h>

/* transition modes for egm_go */
#define EGM_XFADE 0u /* on the next bar line; the old song fades under the new over a bar */
#define EGM_DROP 1u  /* on the next bar line; layers the new song lacks cut on the downbeat (bass dropout) */
#define EGM_BEAT 2u  /* on the next beat, hard (stingers) */

void egm_init(void);
void egm_play(uint8_t mood, uint8_t biome, uint16_t seed); /* start now, fading in */
void egm_go(uint8_t mood, uint8_t biome, uint8_t mode);    /* change song, quantised */
void egm_intensity(uint8_t level);                         /* 0..3, applied on the next bar line */
void egm_stop(void);
void egm_sfx(uint8_t id);
void egm_music_volume(uint8_t v, uint8_t rate); /* 0..8; rate: frames per gain step (0: now) */
void egm_sfx_volume(uint8_t v);                 /* 0..8 */
void egm_master(uint8_t level, uint8_t rate);   /* NR50 0..7 (pause / game over fades both) */
void egm_tick(void);

/* introspection for the test harness */
extern uint8_t egm_row, egm_bar, egm_chord, egm_mood;
#endif
