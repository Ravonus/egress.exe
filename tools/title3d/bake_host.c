/* The title bake's renderer: the same v2 core C as the cartridge's live renderer (proto/raycast/v2/src/core2.c, which
 * the harness proves byte-identical to the cartridge asm), built for the bake's view size, run over a whole camera
 * list in one process.
 *   bake_host map.bin cams.bin out.bin
 * map.bin: the 32x32 map; cams.bin: n x (cx, cy, ca) little-endian uint16; out.bin: n framebuffers (BUFSZ each,
 * column-major tile strips: tile (tx, ty) is bytes (tx * TH + ty) * 16 .. +16). */
#include <stdio.h>
#include <string.h>
#include "ray2.h"

static uint8_t fb[BUFSZ];

static int rd16(FILE *f, uint16_t *v) {
  int a = fgetc(f), b = fgetc(f);
  if (a < 0 || b < 0) return 0;
  *v = (uint16_t)(a | (b << 8));
  return 1;
}

int main(int argc, char **argv) {
  FILE *f, *c, *o;
  uint16_t x, y, a;
  if (argc != 4) return 2;
  f = fopen(argv[1], "rb");
  if (!f || fread(map, 1, 1024, f) != 1024) return 1;
  fclose(f);
  c = fopen(argv[2], "rb");
  o = fopen(argv[3], "wb");
  if (!c || !o) return 1;
  while (rd16(c, &x) && rd16(c, &y) && rd16(c, &a)) {
    cam_x = x;
    cam_y = y;
    cam_a = a;
    memset(fb, 0x5A, sizeof fb);
    cast_all();
    walls_all(fb);
    floors_all(fb);
    fwrite(fb, 1, BUFSZ, o);
  }
  fclose(c);
  fclose(o);
  return 0;
}
