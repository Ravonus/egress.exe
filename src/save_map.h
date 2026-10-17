#ifndef EG_SAVE_MAP_H
#define EG_SAVE_MAP_H
/* Native 8 KiB SRAM allocation. Keep these boundaries in step with redesign.md 9.5. */
#define SAVE_META_VERSION 2u
#define SAVE_CONT_VERSION 2u
#define SAVE_FAMILY_VERSION 3u
#define SAVE_PREF_VERSION 2u
#define SAVE_META_A 0xA000u
#define SAVE_META_B 0xA020u
#define SAVE_CONT_A 0xA040u
#define SAVE_CONT_B 0xA140u
#define SAVE_CONT_BYTES 256u
#define SAVE_SKY 0xA240u
#define SAVE_LINE_A 0xA2C0u
#define SAVE_LINE_B 0xA300u
#define SAVE_LINE_BYTES 64u
#define SAVE_FAMILY_A 0xA570u
#define SAVE_FAMILY_B 0xAA6Cu
#define SAVE_FAMILY_BYTES 1276u
#define SAVE_PREF_A 0xAF68u
#define SAVE_PREF_B 0xAF78u
#define SAVE_PREF_BYTES 16u
#define SAVE_CLOCK 0xB4C0u
#define SAVE_HOST_CLOCK 0xB4E0u
#define SAVE_RTC 0xB4F0u
#define SAVE_STATS_A 0xBC00u
#define SAVE_STATS_B 0xBD80u
#define SAVE_STATS_BYTES 384u
#endif
