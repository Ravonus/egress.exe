/* The B2 bosses' baked art (tools/bake_boss.py -> src/gen/boss_art_data.h), and everything that reads it: the data
 * lives in this file's bank, so only these functions touch it.
 *
 * VRAM on a boss floor (boss floors only; a normal floor's look_load puts everything back):
 *   sprite tiles 158..205 (B1's ST_BOSS range): the Tenant's stamps, shadow, beam, marker, shard (24 tiles), or LAST
 *     FAX's three poses, the lane chevron and the shard (44 tiles);
 *   BG tiles in VRAM bank 0's BG-only block (0x9000, signed addressing): ids 84..127 then 0..15, the Tenant's body
 *     and its three screens (BA_BG_N tiles, at most 60).
 * Palettes: BG 1 the Tenant's body, BG 7 its screen, OBJ 3 the stamps or LAST FAX. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "boss_int.h"
#include "gen/boss_art_data.h"

void boss_art_load(uint8_t kind) BANKED {
  VBK_REG = 0;
  if (kind == BOSS_BIOME) {
    set_sprite_data(BA_SPR0, BA_TEN_SPR_N, ba_ten_spr);
    if (BA_BG_N > 44u) {
      set_bkg_data(84u, 44u, ba_bg_tiles);
      set_bkg_data(0u, (uint8_t)(BA_BG_N - 44u), ba_bg_tiles + 44u * 16u);
    } else
      set_bkg_data(84u, BA_BG_N, ba_bg_tiles);
  } else
    set_sprite_data(BA_SPR0, BA_FAX_SPR_N, ba_fax_spr);
}

/* the Tenant's 8x6 tiles over its cells (display off, or VBlank: 96 bytes) */
void boss_art_body(void) BANKED {
  VBK_REG = 1;
  set_bkg_tiles(TEN_CX * 2u, TEN_CY * 2u, BA_BODY_W, BA_BODY_H, ba_body_attr);
  VBK_REG = 0;
  set_bkg_tiles(TEN_CX * 2u, TEN_CY * 2u, BA_BODY_W, BA_BODY_H, ba_body_tile);
}

void boss_art_face(uint8_t v) BANKED {
  uint8_t x = (uint8_t)(TEN_CX * 2u + BA_FACE_X), y = (uint8_t)(TEN_CY * 2u + BA_FACE_Y);
  VBK_REG = 1;
  set_bkg_tiles(x, y, BA_FACE_W, BA_FACE_H, ba_face_attr[v]);
  VBK_REG = 0;
  set_bkg_tiles(x, y, BA_FACE_W, BA_FACE_H, ba_face_tile[v]);
}

void boss_art_pals(uint8_t kind, uint16_t *bg1, uint16_t *bg7, uint16_t *obj3) BANKED {
  memcpy(bg1, ba_pal_ten_body, 8);
  memcpy(bg7, ba_pal_ten_face, 8);
  memcpy(obj3, kind == BOSS_BIOME ? ba_pal_ten_obj : ba_pal_fax_obj, 8);
}
