/* Exact native CRC implementation, independent of gameplay/save fixtures. */
#include <gb/gb.h>
#include <gb/cgb.h>
#include "core/eg_journal.h"
uint8_t journal_data[384];
volatile uint16_t journal_size,journal_value;
volatile uint8_t journal_ready,journal_request,journal_done;
void main(void){
 DISPLAY_OFF;cpu_fast();HIDE_BKG;HIDE_WIN;HIDE_SPRITES;DISPLAY_ON;journal_ready=1u;
 while(1){
  vsync();
  if(journal_request!=journal_done){
   journal_value=eg_save_crc(journal_data,journal_size);
   journal_done=journal_request;
  }
 }
}
