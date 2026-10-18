#include <gb/gb.h>
#include <gb/cgb.h>
const uint8_t look_bits[8]={1u,2u,4u,8u,16u,32u,64u,128u};
uint8_t look_present[28],roam_hidden[28];
volatile uint8_t scan_request,scan_done,scan_start,scan_count,scan_axis,scan_value,scan_ready;
#include "look_scan.inc"
#include "sprites_start.inc"
void main(void){
  DISPLAY_OFF;cpu_fast();HIDE_BKG;HIDE_WIN;HIDE_SPRITES;DISPLAY_ON;scan_ready=1u;
  while(1){
    vsync();
    if(scan_request!=scan_done){
      scan_value=scan_axis==2u ? sprite_start(scan_start,scan_count) : scan_axis ? missing_column(scan_start,scan_count) : missing_row(scan_start,scan_count);
      scan_done=scan_request;
    }
  }
}
