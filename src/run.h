#ifndef RUN_H
#define RUN_H
#include <gb/gb.h>
#include <stdint.h>

#define RUN_FLOORS 10u /* office stage deck, LAST FAX at 5, THE TENANT at 10 */

void run_begin(void) BANKED;      /* a new run: mods, factions, the truth matrix seeded from the saved trace */
void run_floor_done(void) BANKED; /* the floor's acts move factions and the matrix; unlocks */
void run_end(uint8_t won) BANKED; /* the meta save */
void run_record_end(uint8_t won) BANKED; /* idempotent death/egress transaction, including the continue tombstone */
void run_resume_get(uint8_t *out2) BANKED;
void run_resume_set(const uint8_t *in2) BANKED;
uint8_t run_save_serial(void) BANKED;
void run_room_forgot(void) BANKED; /* B1: a held-SELECT reset leans the matrix once */
void landing_screen(void) BANKED; /* someone talks, then trades */
void ending_screen(void) BANKED;  /* a question, from what the matrix leans toward */

/* the battery save (two CRC-16 records in SRAM bank 0) */
typedef struct {
  uint16_t runs;
  uint8_t wins, best_floor, unlocked; /* unlocked: MOD_* bits that enter the trader pool */
  uint16_t seen;                      /* the book: SEEN_* bits */
  uint8_t trace[9];                   /* the truth matrix's bounded trace across runs */
} meta_t;
#define SEEN_MITE 0x0001u
#define SEEN_EYE 0x0002u
#define SEEN_BOSS 0x0004u
#define SEEN_TALKER0 0x0008u /* << faction */
extern meta_t meta;
extern uint8_t meta_loaded; /* 1: a valid record was found at power-on */
void save_load(void) BANKED;
void save_write(void) BANKED;

#endif
