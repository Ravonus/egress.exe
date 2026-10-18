#include <gb/gb.h>
#include <gb/cgb.h>
uint8_t scan_data[224],scan_request,scan_done,scan_start,scan_rows,scan_count,scan_ready;
uint8_t __at(0xC300) health_cell[4];
uint8_t __at(0xC304) scan_guard[8];
#include "health_scan.inc"
void main(void){
 DISPLAY_OFF;cpu_fast();HIDE_BKG;HIDE_WIN;HIDE_SPRITES;DISPLAY_ON;scan_ready=1u;
 while(1){
  vsync();
  if(scan_request!=scan_done){
   if(scan_count<4u)incoming(scan_data+scan_start,scan_start,scan_rows,scan_count);
   scan_done=scan_request;
  }
 }
}
