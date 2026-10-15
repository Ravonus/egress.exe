/* Bakes the C walls (core2.c walls_all) for short walls: per height h < HB and per (t, u), the 2h slot-0 bytes of
   the rows vt - VOFF .. vt + h - 1 - VOFF. Also checks that slot 1's bytes are slot 0's swapped (the asm ORs
   swap(byte) for slot 1). Output: HB blocks of 1024 entries, 2h bytes each, h = 0.. HB-1 (stdout, binary). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ray2.h"
#include "tex_ids.h"

static uint8_t fb[BUFSZ];

int main(int argc, char **argv) {
  int hb = argc > 1 ? atoi(argv[1]) : 16, h, idx, i, bad = 0;
  for (h = 0; h < hb; h++)
    for (idx = 0; idx < 1024; idx++) {
      uint8_t t = (uint8_t)(idx >> 4), u = (uint8_t)(idx & 15);
      int r0 = T_VTOP[h] - VOFF;
      if ((t >> 1) >= NWALLTEX) t = 0;
      memset(fb, 0, sizeof fb);
      for (i = 0; i < NRAYS; i++) { ray_h[i] = (uint8_t)h; ray_t[i] = t; ray_u[i] = u; }
      walls_all(fb);
      if (r0 < 0 || r0 + h > VIEW_H) { fprintf(stderr, "h %d clips\n", h); return 1; }
      for (i = 0; i < 2 * h; i++) {
        uint8_t b = fb[r0 * 2 + i];
        if ((b & 0x0Fu) != (b >> 4)) bad++;
        putchar(b & 0xF0u);
      }
      /* nothing outside the wall rows */
      for (i = 0; i < STRIP; i++)
        if ((i < r0 * 2 || i >= (r0 + h) * 2) && fb[i]) { fprintf(stderr, "h %d writes outside\n", h); return 1; }
    }
  if (bad) { fprintf(stderr, "slot 1 is not slot 0 swapped (%d bytes)\n", bad); return 1; }
  return 0;
}
