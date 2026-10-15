; EGRESS sky engine: 16 x 16 -> 32 multiplies for SM83 (SDCC sdcccall(1): a in DE, b in BC, result in DEBC).
; Two 16 x 8 shift-add passes (unrolled) instead of the library's 32 x 32 __mullong.
	.module sky_mul
	.area _HOME
	.globl _sky_mul16, _sky_mulu16

; int32_t sky_mul16(int16_t a, int16_t b)
_sky_mul16::
	ld	a, d
	xor	a, b
	push	af
	bit	7, d
	jr	z, 1$
	xor	a, a
	sub	a, e
	ld	e, a
	ld	a, #0
	sbc	a, d
	ld	d, a
1$:
	bit	7, b
	jr	z, 2$
	xor	a, a
	sub	a, c
	ld	c, a
	ld	a, #0
	sbc	a, b
	ld	b, a
2$:
	call	mulu_
	pop	af
	bit	7, a
	ret	z
	xor	a, a
	sub	a, c
	ld	c, a
	ld	a, #0
	sbc	a, b
	ld	b, a
	ld	a, #0
	sbc	a, e
	ld	e, a
	ld	a, #0
	sbc	a, d
	ld	d, a
	ret

; uint32_t sky_mulu16(uint16_t a, uint16_t b)
_sky_mulu16::
mulu_:
	push	bc
	ld	hl, #0
	xor	a, a
	add	hl, hl
	rla
	sla	c
	jr	nc, 30$
	add	hl, de
	adc	a, #0
30$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 31$
	add	hl, de
	adc	a, #0
31$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 32$
	add	hl, de
	adc	a, #0
32$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 33$
	add	hl, de
	adc	a, #0
33$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 34$
	add	hl, de
	adc	a, #0
34$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 35$
	add	hl, de
	adc	a, #0
35$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 36$
	add	hl, de
	adc	a, #0
36$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 37$
	add	hl, de
	adc	a, #0
37$:
	ld	c, l
	ld	b, h
	pop	hl
	push	bc
	ld	c, h
	ld	b, a
	ld	hl, #0
	xor	a, a
	add	hl, hl
	rla
	sla	c
	jr	nc, 40$
	add	hl, de
	adc	a, #0
40$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 41$
	add	hl, de
	adc	a, #0
41$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 42$
	add	hl, de
	adc	a, #0
42$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 43$
	add	hl, de
	adc	a, #0
43$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 44$
	add	hl, de
	adc	a, #0
44$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 45$
	add	hl, de
	adc	a, #0
45$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 46$
	add	hl, de
	adc	a, #0
46$:
	add	hl, hl
	rla
	sla	c
	jr	nc, 47$
	add	hl, de
	adc	a, #0
47$:
	ld	d, a
	ld	e, b
	pop	bc
	ld	a, b
	add	a, l
	ld	b, a
	ld	a, e
	adc	a, h
	ld	e, a
	ld	a, d
	adc	a, #0
	ld	d, a
	ret
