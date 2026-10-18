/* Incoming medicine occupies four-cell backing packets. Scan those packets
 * in registers, rather than paying a general C loop for every empty cell.
 * BC carries row count/cell, HL source, DE the bounded four-entry output. */
static void incoming(uint8_t *source,uint8_t cell,uint8_t rows,uint8_t count) __sdcccall(0) __naked {
 source;cell;rows;count;
 __asm
 push bc
 push de
 ldhl sp, #6
 ld a, (hl+)
 ld e, a
 ld a, (hl+)
 ld d, a
 ld a, (hl+)
 ld c, a
 ld a, (hl+)
 ld b, a
 ld a, (hl)
 push de
 ld hl, #_health_cell
 add a, l
 ld l, a
 jr NC, 1$
 inc h
1$:
 ld e, l
 ld d, h
 pop hl
2$:
 ld a, (hl+)
 or a, a
 jr Z, 10$
 and a, #0x3f
 cp a, #0x25
 jr NZ, 10$
 ld a, c
 ld (de), a
 inc de
 push hl
 ld hl, #(_health_cell+4)
 ld a, e
 cp a, l
 jr NZ, 11$
 ld a, d
 cp a, h
 jr Z, 91$
11$:
 pop hl
10$:
 inc c
 ld a, (hl+)
 or a, a
 jr Z, 20$
 and a, #0x3f
 cp a, #0x25
 jr NZ, 20$
 ld a, c
 ld (de), a
 inc de
 push hl
 ld hl, #(_health_cell+4)
 ld a, e
 cp a, l
 jr NZ, 21$
 ld a, d
 cp a, h
 jr Z, 91$
21$:
 pop hl
20$:
 inc c
 ld a, (hl+)
 or a, a
 jr Z, 30$
 and a, #0x3f
 cp a, #0x25
 jr NZ, 30$
 ld a, c
 ld (de), a
 inc de
 push hl
 ld hl, #(_health_cell+4)
 ld a, e
 cp a, l
 jr NZ, 31$
 ld a, d
 cp a, h
 jr Z, 91$
31$:
 pop hl
30$:
 inc c
 ld a, (hl+)
 or a, a
 jr Z, 40$
 and a, #0x3f
 cp a, #0x25
 jr NZ, 40$
 ld a, c
 ld (de), a
 inc de
 push hl
 ld hl, #(_health_cell+4)
 ld a, e
 cp a, l
 jr NZ, 41$
 ld a, d
 cp a, h
 jr Z, 91$
41$:
 pop hl
40$:
 inc c
 bit 7, b
 jr Z, 60$
 ld a, l
 add a, #12
 ld l, a
 jr NC, 61$
 inc h
61$:
 ld a, c
 add a, #12
 ld c, a
60$:
 dec b
 ld a, b
 and a, #127
 jp NZ, 2$
 jr 90$
91$:
 pop hl
90$:
 pop de
 pop bc
 ret
 __endasm;
}
