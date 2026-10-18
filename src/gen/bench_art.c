#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
void bench_piece_0(uint8_t,uint8_t,uint8_t) BANKED;
void bench_piece_1(uint8_t,uint8_t,uint8_t) BANKED;
void bench_fuse_0(uint8_t,uint8_t) BANKED;
void bench_fuse_1(uint8_t,uint8_t) BANKED;
void bench_fuse_2(uint8_t,uint8_t) BANKED;
void bench_fuse_3(uint8_t,uint8_t) BANKED;
void bench_fuse_4(uint8_t,uint8_t) BANKED;
void bench_fuse_5(uint8_t,uint8_t) BANKED;
void bench_fuse_6(uint8_t,uint8_t) BANKED;
void bench_fuse_7(uint8_t,uint8_t) BANKED;
void bench_fuse_8(uint8_t,uint8_t) BANKED;
void bench_fuse_9(uint8_t,uint8_t) BANKED;
void bench_fuse_10(uint8_t,uint8_t) BANKED;
void bench_art_piece(uint8_t p,uint8_t pose,uint8_t tile) BANKED {if(!p || p>16u)return;if(p<=8u)bench_piece_0(p,pose,tile);else bench_piece_1(p,pose,tile);}
void bench_art_fuse(uint8_t result,uint8_t step,uint8_t tile) BANKED {switch(result) {
case 13u:bench_fuse_0(step,tile);break;
case 8u:bench_fuse_1(step,tile);break;
case 14u:bench_fuse_2(step,tile);break;
case 15u:bench_fuse_3(step,tile);break;
case 7u:bench_fuse_4(step,tile);break;
case 6u:bench_fuse_5(step,tile);break;
case 16u:bench_fuse_6(step,tile);break;
case 9u:bench_fuse_7(step,tile);break;
case 10u:bench_fuse_8(step,tile);break;
case 11u:bench_fuse_9(step,tile);break;
case 12u:bench_fuse_10(step,tile);break;
}}
static const uint16_t palettes[16][4]={
{0,9383,6655,14239},
{0,9383,30282,32730},
{0,9383,24370,29659},
{0,9383,24370,29659},
{0,9383,31482,32735},
{0,9383,11071,32735},
{0,9383,30282,32730},
{0,9383,11071,32735},
{0,9383,30282,32730},
{0,9383,30282,32730},
{0,9383,6655,14239},
{0,9383,11071,32735},
{0,9383,30282,32730},
{0,9383,6655,14239},
{0,9383,31482,32735},
{0,9383,24370,29659},
};
void bench_art_palette(uint8_t p,uint8_t slot) BANKED {if(p && p<=16u)set_sprite_palette(slot,1u,palettes[p-1u]);}
