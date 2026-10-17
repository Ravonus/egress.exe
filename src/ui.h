#ifndef UI_H
#define UI_H
#include "play.h"
/* Four spare bank-1 tiles: shared by menus and the pause window without
 * replacing any floor, inventory, font or sprite art. */
#define UI_DARK 6u
#define UI_LIGHT 7u
void ui_load(void) BANKED;
void ui_colours(void) BANKED;
void ui_fill(uint8_t win,uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint8_t pal) BANKED;
void ui_frame(uint8_t win,uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint8_t pal) BANKED;
void ui_shell(void) BANKED;
/* Strings stay in the calling ROM bank; a BANKED helper cannot dereference
 * a caller's ordinary const pointer after switching banks. */
#define ui_page(title,footer) do {ui_shell();text_at(0u,2u,1u,title,UI_DARK);text_at(0u,0u,17u,footer,UI_LIGHT);} while(0)
#endif
