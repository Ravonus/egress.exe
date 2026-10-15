#ifndef OAM_H
#define OAM_H
/* B2: the per-line sprite budget, priority and flicker (docs/b2-combat-api.md). Owned by sprites.c (b2-mobs).
 *
 * The hardware shows at most 10 OBJs on a scan line and silently drops the rest (lowest OAM index wins). draw_sprites
 * never lets that happen: every OBJ it places is counted in 4-line bands of OAM y (an 8x16 OBJ at OAM y `oy` covers
 * lines oy .. oy+15, so 4 or 5 bands), and an OBJ (or a group of OBJs that must show together, like a mob's body)
 * that would make any band reach 11 is refused. The count is conservative by at most 3 lines.
 *
 * Priority is drawing order. draw_sprites places, in this order:
 *   OP_HEIR    the heir (4 OBJs)                  never refused (placed first, the bands start empty)
 *   OP_WEAPON  the lead's arc, the charge spark
 *   OP_MARK    elite crowns, boss markers         markers before bodies: you always see what is dangerous
 *   OP_BOSS    a boss body (boss.c draws through oam_group)
 *   OP_SHOT    enemy and returned orbs            a shot you can't see is a shot you can't parry
 *   OP_MOB     mob bodies, a rotating start       oam_spin rotates the first mob each frame, so a crowd on one line
 *                                                 flickers fairly instead of one mob vanishing
 *   OP_OVER    mob overlays (accessories, tag tells), elite outline bits
 *   OP_FX      particles and puffs                dropped first, never flickered for
 * A caller in another module that adds sprites draws them from draw_sprites in its class's place (b2-boss: call
 * oam_group for the boss body where the OP_BOSS comment is in draw_sprites, and use oam_spin for summons).
 *
 * OAM slots 24..39 are the look's flying cards while the room assembles or collapses (src/look.h); during that
 * draw_sprites keeps below 24 (oam_cap). */
#include <gb/gb.h>
#include <stdint.h>

#define OAM_LINE_MAX 10u
#define OAM_BANDS 42u /* 4-line bands over OAM y 0..167 */
enum { OP_HEIR = 0, OP_WEAPON, OP_MARK, OP_BOSS, OP_SHOT, OP_MOB, OP_OVER, OP_FX };

extern uint8_t oam_n;       /* the next free OAM slot this frame */
extern uint8_t oam_cap;     /* slots usable this frame (40, or 24 while the look's cards fly) */
extern uint8_t oam_spin;    /* +1 per frame: the rotating start for flickering groups (mobs, summons) */
extern uint8_t oam_refused; /* OBJs refused this frame (harness; flicker and dropped particles) */

/* Place `n` 8x16 OBJs side by side, 8 px apart, at screen-world-relative screen x `sx`, screen y `sy` (the top of
 * the OBJ; OAM y = sy + 16, OAM x = sx + 8), tiles tile0, tile0 + tstep, ... (tstep 2 in 8x16 mode), attribute
 * `prop`. All or nothing: returns 1 if every OBJ fit the per-line budget and the slots, else 0 (nothing placed).
 * OBJs wholly off screen are skipped (not counted); a group partly off screen places the visible ones. */
uint8_t oam_group(int16_t sx, int16_t sy, uint8_t n, uint8_t tile0, uint8_t tstep, uint8_t prop) NONBANKED;
/* the same check without placing */
uint8_t oam_fits(int16_t sy, uint8_t n) NONBANKED;
/* count OBJs placed directly into shadow_OAM[from .. oam_n) (the heir's own writer) */
void oam_count(uint8_t from) NONBANKED;
/* the busiest band's count so far this frame (harness) */
uint8_t oam_band_max(void) NONBANKED;
/* The look owns fixed high slots. Move its pair against the same live budget,
 * including between actor frames when a VBlank DMA can interrupt the look. */
void oam_card_pair(uint8_t a, uint8_t b, uint8_t x0, uint8_t x1, uint8_t y,
                   uint8_t tile, uint8_t prop) NONBANKED;

#endif
