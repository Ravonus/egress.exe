/* OBJ billboards (v2): software pre-scaled frames, column-occluded, uploaded by GDMA in the second VBlank */
#ifndef OBJ2_H
#define OBJ2_H
#include <stdint.h>
#ifndef OBJ_ON
#define OBJ_ON 0
#endif
#if OBJ_ON
void obj_init(void);
void obj_build(uint8_t cur);
void obj_vbl_upload(void);
void obj_vbl_flip(void);
#else
#define obj_init()
#define obj_build(c)
#endif
#endif
