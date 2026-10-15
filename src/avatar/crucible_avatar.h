/* Ported from CRUCIBLE.EXE (cartridge/src, MIT, the same owner) for EGRESS.EXE: the procedural face generator, so
 * EGRESS's talkers and the player share CRUCIBLE's faces. Changes: the base layer's scratch lives in SRAM bank 0
 * (EGRESS has one 8 KiB bank), and the CRUCIBLE-only BG/cache helpers are replaced by avatar_bg_load (EGRESS). */
#ifndef CRUCIBLE_AVATAR_H
#define CRUCIBLE_AVATAR_H
/* Procedural talking faces (crucible_avatar.c): 48x48, 36 sprite tiles, no art data. */
#define AVATAR_BYTES 576u
/* genome byte 5 bits 3..4: an optional human look, independent of the cosmetic unlocks */
#define AV_LOOK_DREAM 0u
#define AV_LOOK_BOY 1u
#define AV_LOOK_GIRL 2u
#define AV_LOOK_MASK 0x18u
void avatar_make(uint16_t seed, uint8_t faction) BANKED; /* faction 0..5 (CRU_FAC_*) */
void avatar_say(char c) BANKED; /* the letter being typed: shapes the mouth */
uint8_t avatar_tick(uint8_t dt) BANKED; /* 1: the face changed (stream it) */
void avatar_glitch(uint8_t in) BANKED; /* 1: glitch in (rows settle), 0: glitch out (rows tear away) */
void avatar_fx(uint8_t fx) BANKED; /* a shader on top (not the aura) */
uint8_t avatar_bob(void) BANKED; /* 0/1 pixel idle bob */
void avatar_palette(uint16_t *out) BANKED;
uint8_t avatar_flash(void) BANKED; /* 1 on the frame a lightning bolt strikes */
extern uint8_t avatar_still; /* 1: the face does not bob (fights) */
void avatar_make_genome(const uint8_t *g) BANKED; /* the player's face from its 6-byte genome (fight spec 8.1) */
void avatar_bg_load(uint8_t first) BANKED; /* EGRESS: the 36 tiles into VRAM bank 1 background tiles first.. */
#endif
