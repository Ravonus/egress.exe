/* Host build of the raycaster core: reads a cartridge snapshot (map + camera), writes the rays and framebuffer the
   same C produces on the host, so measure.py can diff them against the ROM's WRAM. */
#include <stdio.h>
#include <string.h>
#include "ray.h"

static uint8_t fb[BUFSZ];

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
  spr_x = rd16(f);
  spr_y = rd16(f);
  spr_on = (uint8_t)fgetc(f);
  fclose(f);
  memset(fb, 0x5A, sizeof fb);
  cast_all();
  fill_all(fb);
  sprite_draw(fb);
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
