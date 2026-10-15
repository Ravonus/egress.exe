#ifndef EG_METEOR_TAB_H
#define EG_METEOR_TAB_H
#include <stdint.h>
#define METEOR_NSHOWER 11
#define METEOR_REC 111u
#define METEOR_Y0 2026
extern const uint8_t meteor_tab[1225];
/* peak minute (since 2026-01-01 UTC) of shower s in year y (2026..2126) */
static inline uint32_t meteor_peak(uint8_t s, uint8_t yi) {
  const uint8_t *r = meteor_tab + 4 + (uint16_t)s * METEOR_REC;
  uint32_t base = r[4] | ((uint32_t)r[5] << 8) | ((uint32_t)r[6] << 16) | ((uint32_t)r[7] << 24);
  uint32_t step = 525000ul + (r[8] | ((uint16_t)r[9] << 8));
  return base + (uint32_t)yi * step + (int32_t)(int8_t)r[10 + yi] * r[3];
}
#endif
