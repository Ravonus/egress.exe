/* Permanent discoveries are independent of finite carried quantities. The
 * cached record lives after floor art, inside the inactive chooser's buffer.
 * Flush and disable it before that chooser runs; reload after choosing. */
#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "collection.h"
#include "core/eg_collection.h"
#include "core/eg_journal.h"
#include "egress.h"
#include "heir.h"
#include "tutorial.h"
#include "save_map.h"
#include "mapper.h"
uint8_t collection_ready;
static uint8_t __at(0xDDD7) collection_slot;
uint8_t __at(0xDDD8) collection_dirty;
static uint8_t __at(0xDE38) collection_cache[64];
typedef char collection_layout[(SAVE_COLLECTION_A+64u==SAVE_COLLECTION_B && SAVE_COLLECTION_B+64u<=SAVE_FAMILY_A && 0xDE38u+64u<=0xDE78u) ? 1 : -1];
void collection_load(void) BANKED {
  uint8_t chosen;
  EG_SRAM_OPEN();chosen=eg_collection_choose((uint8_t *)SAVE_COLLECTION_A,(uint8_t *)SAVE_COLLECTION_B);
  if(chosen!=255u)memcpy(collection_cache,(uint8_t *)(chosen ? SAVE_COLLECTION_B : SAVE_COLLECTION_A),64u);
  EG_SRAM_CLOSE();
  if(chosen==255u)eg_collection_empty(collection_cache);
  collection_slot=chosen==255u ? 1u : chosen;collection_dirty=0u;collection_ready=1u;
}
void collection_flush(void) BANKED {
  uint8_t *out;
  if(!collection_ready || !collection_dirty || tutorial_on==2u || dbg_heir)return;
  collection_cache[1]++;eg_save_seal(collection_cache,64u);collection_slot^=1u;
  out=(uint8_t *)(collection_slot ? SAVE_COLLECTION_B : SAVE_COLLECTION_A);
  EG_SRAM_OPEN();out[0]=0u;memcpy(out+1u,collection_cache+1u,63u);out[0]=EG_COLLECTION_VERSION;EG_SRAM_CLOSE();collection_dirty=0u;
}
void collection_pause(void) BANKED {collection_flush();collection_ready=0u;}
uint8_t collection_mark(eg_item_id id) BANKED {
  if(!collection_ready || tutorial_on==2u || dbg_heir || !eg_collection_mark(collection_cache,id))return 0u;
  collection_dirty=1u;return 1u;
}
uint8_t collection_known(eg_item_id id) BANKED {return collection_ready ? eg_discovery_has(collection_cache+4u,id) : 0u;}
uint16_t collection_count(void) BANKED {return collection_ready ? (uint16_t)(collection_cache[2]|((uint16_t)collection_cache[3]<<8)) : 0u;}
