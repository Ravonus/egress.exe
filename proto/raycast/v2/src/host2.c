/* Host build of the v2 core: reads a cartridge snapshot (map, camera), writes rays + framebuffer. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ray2.h"

static uint8_t fb[BUFSZ];
#ifdef STATS
unsigned st_fskip, st_ffast, st_fpart, st_steps, st_runs, st_allruns, st_rows[2][4];
#endif

static uint16_t rd16(FILE *f) {
  int a = fgetc(f), b = fgetc(f);
  return (uint16_t)(a | (b << 8));
}

int main(int argc, char **argv) {
  FILE *f, *o;
  int i;
  if (argc != 3) return 2;
  f = fopen(argv[1], "rb");
  if (!f || fread(map, 1, sizeof map, f) != sizeof map) return 1;
  cam_x = rd16(f);
  cam_y = rd16(f);
  cam_a = rd16(f);
#ifdef FLAT
  { int c = fgetc(f); f_ph = (uint8_t)(c < 0 ? 0 : c); }
#endif
  fclose(f);
  memset(fb, 0x5A, sizeof fb);
  cast_all();
  walls_all(fb);
#ifndef NOFLOOR
  floors_all(fb);
#endif
#ifdef STATS
  fprintf(stderr, "steps %u runs %u vis %u rows s0 %u %u %u %u s1 %u %u %u %u\n", st_steps, st_allruns, st_runs, st_rows[0][0], st_rows[0][1], st_rows[0][2], st_rows[0][3], st_rows[1][0], st_rows[1][1], st_rows[1][2], st_rows[1][3]);
#endif
#ifdef STATS
  { extern unsigned st_mid; fprintf(stderr, "mid rays %u\n", st_mid); }
  fprintf(stderr, "floor tiles skip %u fast %u part %u\n", st_fskip, st_ffast, st_fpart);
#endif
#ifdef FLAT
  if (getenv("HOSTDBG")) fprintf(stderr, "bset %u %u %u %u ph %u", f_bset[0], f_bset[1], f_bset[2], NFB > 3 ? f_bset[3] : 0, f_ph);
#endif
  o = fopen(argv[2], "wb");
  fwrite(ray_h, 1, NRAYS, o);
  fwrite(ray_u, 1, NRAYS, o);
  fwrite(ray_t, 1, NRAYS, o);
  for (i = 0; i < NRAYS; i++) {
    fputc(ray_d[i] & 255, o);
    fputc(ray_d[i] >> 8, o);
  }
  fwrite(fb, 1, BUFSZ, o);
  fclose(o);
  return 0;
}
