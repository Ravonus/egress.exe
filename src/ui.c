/* Native 160x144 panels. Tile flips supply all four corners from one tile. */
#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include <string.h>
#include "ui.h"
static const uint8_t dot[16]={0,0,0,0,0,0,0,0,0,0,0,0,0x10,0,0,0};
static const uint8_t edges[48]={
  0,0,0,0,0x0f,0,0x10,0x0f,0x20,0x10,0x20,0x10,0x20,0x10,0x20,0x10,
  0,0,0,0,0xff,0,0,0xff,0,0,0,0,0,0,0,0,
  0x20,0x10,0x20,0x10,0x20,0x10,0x20,0x10,0x20,0x10,0x20,0x10,0x20,0x10,0x20,0x10
};
void ui_load(void) BANKED {
  VBK_REG=1u;set_bkg_data(1u,1u,dot);set_bkg_data(253u,3u,edges);VBK_REG=0u;
}
void ui_colours(void) BANKED {
  pal_bg[24]=RGB(4,3,8);pal_bg[25]=RGB(8,6,13);pal_bg[26]=RGB(19,14,24);pal_bg[27]=RGB(29,27,31);
  pal_bg[28]=RGB(29,27,24);pal_bg[29]=RGB(24,20,22);pal_bg[30]=RGB(17,11,21);pal_bg[31]=RGB(5,4,10);
}
static void cell(uint8_t win,uint8_t x,uint8_t y,uint8_t t,uint8_t attr) {
  attr|=8u;VBK_REG=1u;
  if(win)set_win_tiles(x,y,1u,1u,&attr);else set_bkg_tiles(x,y,1u,1u,&attr);
  VBK_REG=0u;if(win)set_win_tiles(x,y,1u,1u,&t);else set_bkg_tiles(x,y,1u,1u,&t);
}
void ui_fill(uint8_t win,uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint8_t pal) BANKED {
  uint8_t row[20],r;
  memset(row,pal|8u,w);VBK_REG=1u;
  for(r=0u;r<h;r++) {if(win)set_win_tiles(x,y+r,w,1u,row);else set_bkg_tiles(x,y+r,w,1u,row);}
  memset(row,0,w);VBK_REG=0u;
  for(r=0u;r<h;r++) {if(win)set_win_tiles(x,y+r,w,1u,row);else set_bkg_tiles(x,y+r,w,1u,row);}
}
void ui_frame(uint8_t win,uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint8_t pal) BANKED {
  uint8_t k;
  for(k=1u;k<w-1u;k++) {cell(win,x+k,y,254u,pal);cell(win,x+k,y+h-1u,254u,pal|64u);}
  for(k=1u;k<h-1u;k++) {cell(win,x,y+k,255u,pal);cell(win,x+w-1u,y+k,255u,pal|32u);}
  cell(win,x,y,253u,pal);cell(win,x+w-1u,y,253u,pal|32u);
  cell(win,x,y+h-1u,253u,pal|64u);cell(win,x+w-1u,y+h-1u,253u,pal|96u);
}
void ui_shell(void) BANKED {
  uint8_t y,row[20];
  memset(row,1u,sizeof row);ui_fill(0u,0u,0u,20u,18u,UI_DARK);
  for(y=3u;y<17u;y+=2u)set_bkg_tiles(0u,y,20u,1u,row);
  ui_frame(0u,0u,0u,20u,3u,UI_DARK);
  ui_fill(0u,0u,17u,20u,1u,UI_LIGHT);
}
