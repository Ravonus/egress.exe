#ifndef EG_PIECE_H
#define EG_PIECE_H
/* The super matrix's shared core (docs/redesign.md section 5), as far as B1's puzzle rooms need it: the 5-byte piece
 * schema (5.1), EGRESS's recipe subset for the office (section 3), and the one tag x tag reaction table (5.2) with
 * what its reactions do to room cells. Shared verbatim by the cartridge and the host (solver, bake, tests).
 *
 * An item's traits are its tags, so a crafted thing reacts exactly as its traits say: FIRE is HOT and burns paper,
 * SNOW is COLD and freezes a flood, ELECTRIC carries the SHOCK status and shorts through water, BRICK is a weight.
 *
 * Portable C for SDCC (SM83) and host compilers. */
#include <stdint.h>
#include "eg_room.h"

/* tags: byte 2 of a piece (5.1) */
enum { TAG_HOT = 0, TAG_COLD, TAG_WET, TAG_SHINY, TAG_ALIVE, TAG_MAGIC, TAG_GLITCH, TAG_STONE, EG_TAGS };
#define TG(t) ((uint8_t)(1u << (t)))

/* byte 0: trigger (5 bits) | stack group (3 bits) */
enum {
  TRG_HIT = 0, TRG_KILL, TRG_DODGE_START, TRG_DODGE_END, TRG_PARRY, TRG_PERFECT, TRG_CHARGE, TRG_HURT, TRG_LOWHP,
  TRG_PUSH, TRG_DIG, TRG_RECIPE, TRG_ENTER, TRG_EVERY, TRG_COMBO3,
  TRG_USE /* used from the pocket on a cell (the room's verb) */
};
enum { SG_DAMAGE = 0, SG_STATUS, SG_AREA, SG_PROJ, SG_DEFENSE, SG_SPEED, SG_SUMMON, SG_REACT };
/* byte 1: effect (5 bits) | shape (3 bits) */
enum {
  EFF_NONE = 0, EFF_PROJ, EFF_BURST, EFF_WET, EFF_BURN, EFF_FREEZE, EFF_SHOCK, EFF_BLIND, EFF_SLOW, EFF_MARK,
  EFF_TERRAIN, EFF_LAY /* lays a solid block (a weight) */, EFF_STAT, EFF_SUMMON, EFF_HEAL, EFF_TOKEN
};
enum { SH_SELF = 0, SH_ARC, SH_PROJ, SH_AREA, SH_TRAIL, SH_AURA, SH_SUMMON, SH_TERRAIN };
/* byte 4: flags */
#define PF_FOUND 0x01u /* lies in rooms */
#define PF_BENCH 0x02u /* made on the bench */
#define PF_COST 0x04u  /* the cost side of a paired trait */
#define PF_RARITY(r) ((uint8_t)((r) << 6))

/* the pieces B1's rooms use (0 = no piece) */
enum {
  P_NONE = 0,
  P_FIRE, P_WATER, P_AIR, P_EARTH, P_METAL, /* found */
  P_ENERGY, P_RAIN, P_MUD,                   /* found or made */
  P_SNOW, P_ICE, P_BRICK, P_ELECTRIC,        /* made: what opens the four tricks */
  P_STEAM, P_LAVA, P_DUST, P_CLOUD,          /* made: plausible, wrong (red-herring results) */
  P_COUNT
};
#define EG_PIECE_BYTES 5u
#define PC_TRIG(p) ((uint8_t)(eg_piece[p][0] >> 3))
#define PC_GROUP(p) ((uint8_t)(eg_piece[p][0] & 7u))
#define PC_EFF(p) ((uint8_t)(eg_piece[p][1] >> 3))
#define PC_SHAPE(p) ((uint8_t)(eg_piece[p][1] & 7u))
#define PC_TAGS(p) (eg_piece[p][2])
#define PC_MAG(p) ((uint8_t)(eg_piece[p][3] >> 4))
#define PC_CD(p) ((uint8_t)(eg_piece[p][3] & 15u))
#define PC_FLAGS(p) (eg_piece[p][4])
extern const uint8_t eg_piece[P_COUNT][EG_PIECE_BYTES]; /* in a switched bank on the cartridge: read it from other
                                                         banks through eg_piece_tags */
uint8_t eg_piece_tags(uint8_t piece) EG_BANKED;

/* the recipe for two pieces (either order), or 0 */
uint8_t eg_recipe(uint8_t a, uint8_t b) EG_BANKED;
#define EG_RECIPES 11u
extern const uint8_t eg_recipes[EG_RECIPES][3]; /* a, b, result */

/* reactions (5.2): one table for rooms and fights */
enum {
  RX_NONE = 0,
  RX_FLARE, RX_THAW, RX_STEAM, RX_GLARE, RX_BURN, RX_EMBER, RX_FLICKER, RX_GLOWHEAT,  /* HOT x ... */
  RX_DEEPFREEZE, RX_FREEZE, RX_PRISM, RX_FROSTBITE, RX_CRYSTAL, RX_STUTTER, RX_BRITTLE, /* COLD x ... */
  RX_SOAK, RX_SLICK, RX_OVERGROW, RX_BLOOM, RX_SHORTWET, RX_MUD,                     /* WET x ... */
  RX_REFLECT, RX_SUNFEED, RX_LENS, RX_GHOST, RX_POLISH,                              /* SHINY x ... */
  RX_SWARM, RX_WILD, RX_ROT, RX_MOSS,                                                /* ALIVE x ... */
  RX_RESONANCE, RX_GLITCHBURST, RX_RUNE,                                             /* MAGIC x ... */
  RX_DUPLICATE, RX_NOCLIP,                                                           /* GLITCH x ... */
  RX_CRUSH,                                                                          /* STONE x STONE */
  RX_SHORT,  /* the SHOCK status meets WET: a short circuit through everything wet and connected */
  RX_WEIGHT, /* a laid weight (EFF_LAY) */
  RX_COUNT
};
extern const uint8_t eg_rx_table[EG_TAGS][EG_TAGS];
/* a cell type's own tag (EG_TAGS: none), so tiles react like mobs */
uint8_t eg_tile_tag(uint8_t type) EG_BANKED;

/* Use `piece` on `cell`. Returns the reaction that changed the room (RX_NONE: nothing happens and the piece is kept).
 * occ[0..nocc) are the cells the player's body covers: a spread doesn't burn, thaw or lay under them. The changed
 * cells are listed in eg_dirty in spread order (nearest first; eg_ndirty > EG_DIRTY_MAX: redraw everything).
 * The verbs B1 uses: BURN (HOT x ALIVE: paper and planks), FREEZE (COLD x WET: a flood to ice), THAW (HOT x COLD:
 * ice back to water), SHORT (SHOCK into water or straight into a socket: sockets powered, dead shutters gone) and
 * WEIGHT (BRICK laid on carpet or a plate). */
uint8_t eg_apply(eg_room *r, uint8_t piece, uint8_t cell, const uint8_t *occ, uint8_t nocc) EG_BANKED;

/* names for the HUD and the bench: copied into out (up to 8 characters + NUL) so any bank can print them */
void eg_piece_name(uint8_t piece, char *out) EG_BANKED;
void eg_piece_abbr(uint8_t piece, char *out) EG_BANKED; /* 4 characters, drawn on the item's cell */
void eg_rx_name(uint8_t rx, char *out) EG_BANKED;        /* up to 12 characters */
void eg_tags_text(uint8_t piece, char *out) EG_BANKED;   /* "HOT WET", up to 18 characters */

#endif
