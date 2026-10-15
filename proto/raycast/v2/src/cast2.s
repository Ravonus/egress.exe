; cast_asm: every ray's DDA (v1's, with 16 texel columns per face: u = frac >> 4)
	.globl _cast_asm, _cam_x, _cam_y, _cam_a, _map, _ray_d, _T_COLOFF, _T_COLCOS, _T_DELTA, _T_COSABS
	.globl _T_HEIGHT

	.area _DATA
c_i:	.ds 1
c_a:	.ds 2
c_ay:	.ds 2
c_xpos:	.ds 1
c_ypos:	.ds 1
c_ddx:	.ds 2
c_ddy:	.ds 2
c_stx:	.ds 2
c_sty:	.ds 2
c_dist:	.ds 2
c_side:	.ds 1
c_cell:	.ds 1
c_base:	.ds 2

	.area _CODE

; hl = a * b  (quarter squares, page-aligned tables at 0x7800: lo bytes 0x7800-0x79FF, hi bytes 0x7A00-0x7BFF)
; in a, b; clobbers a, c, d, e
SQLO = 0x78
mul8:
	ld	c, a
	add	a, b
	ld	l, a
	ld	a, #SQLO
	adc	a, #0
	ld	h, a
	ld	e, (hl)
	inc	h
	inc	h
	ld	d, (hl)
	ld	a, c
	sub	a, b
	jr	nc, 1$
	cpl
	inc	a
1$:
	ld	l, a
	ld	h, #SQLO
	ld	a, e
	sub	a, (hl)
	ld	e, a
	inc	h
	inc	h
	ld	a, d
	sbc	a, (hl)
	ld	h, a
	ld	l, e
	ret

; hl = (de * b) >> 8 ; de 8.8, b 0.8 ; clobbers a, c, d, e
mulhi:
	push	de
	ld	a, e
	call	mul8
	pop	de
	ld	a, h
	push	af
	ld	a, d
	call	mul8
	pop	af
	add	a, l
	ld	l, a
	ret	nc
	inc	h
	ret

_cast_asm::
	ld	a, (#_cam_y+1)
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, (#_cam_x+1)
	add	a, l
	ld	l, a
	ld	de, #_map
	add	hl, de
	ld	a, l
	ld	(c_base), a
	ld	a, h
	ld	(c_base+1), a
	xor	a, a
	ld	(c_i), a
200$:
	; a = (cam_a + coloff[i]) & 1023
	ld	a, (c_i)
	ld	l, a
	ld	h, #0
	add	hl, hl
	ld	de, #_T_COLOFF
	add	hl, de
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	a, (#_cam_a)
	add	a, l
	ld	l, a
	ld	(c_a), a
	ld	(c_ay), a
	ld	a, (#_cam_a+1)
	adc	a, h
	and	a, #3
	ld	h, a
	ld	(c_a+1), a
	; ay = a - 256
	dec	a
	and	a, #3
	ld	(c_ay+1), a
	; xpos = ((h + 1) & 2) == 0 ; ypos = (h & 2) == 0
	ld	a, h
	inc	a
	and	a, #2
	ld	a, #0
	jr	nz, 201$
	inc	a
201$:
	ld	(c_xpos), a
	ld	a, h
	and	a, #2
	ld	a, #0
	jr	nz, 202$
	inc	a
202$:
	ld	(c_ypos), a
	; ddx = T_DELTA[a]
	add	hl, hl
	ld	de, #_T_DELTA
	add	hl, de
	ld	a, (hl+)
	ld	(c_ddx), a
	ld	a, (hl)
	ld	(c_ddx+1), a
	; ddy = T_DELTA[ay]
	ld	hl, #c_ay
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	add	hl, hl
	ld	de, #_T_DELTA
	add	hl, de
	ld	a, (hl+)
	ld	(c_ddy), a
	ld	a, (hl)
	ld	(c_ddy+1), a
	; x side: stx, gx, sdx
	ld	a, (c_xpos)
	or	a, a
	ld	a, (#_cam_x)
	jr	z, 203$
	cpl
	ld	b, a
	ld	a, #1
	ld	(c_stx), a
	xor	a, a
	ld	(c_stx+1), a
	jr	204$
203$:
	ld	b, a
	ld	a, #0xFF
	ld	(c_stx), a
	ld	(c_stx+1), a
204$:
	ld	hl, #c_ddx
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	call	mulhi
	push	hl			; sdx
	; y side: sty, gy, sdy
	ld	a, (c_ypos)
	or	a, a
	ld	a, (#_cam_y)
	jr	z, 205$
	cpl
	ld	b, a
	ld	a, #32
	ld	(c_sty), a
	xor	a, a
	ld	(c_sty+1), a
	jr	206$
205$:
	ld	b, a
	ld	a, #0xE0
	ld	(c_sty), a
	ld	a, #0xFF
	ld	(c_sty+1), a
206$:
	ld	hl, #c_ddy
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	call	mulhi
	ld	d, h
	ld	e, l			; de = sdy
	pop	bc			; bc = sdx
	ld	hl, #c_base
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
210$:
	; DDA: bc = sdx, de = sdy, hl = map cell
	ld	a, b
	cp	a, d
	jr	c, 211$
	jr	nz, 215$
	ld	a, c
	cp	a, e
	jr	nc, 215$
211$:
	; x step
	ld	a, (c_ddx)
	add	a, c
	ld	c, a
	ld	a, (c_ddx+1)
	adc	a, b
	ld	b, a
	push	de
	ld	a, (c_stx)
	ld	e, a
	ld	a, (c_stx+1)
	ld	d, a
	add	hl, de
	pop	de
	ld	a, (hl)
	bit	7, a
	jr	nz, 210$
	ld	(c_cell), a
	xor	a, a
	ld	(c_side), a
	; dist = sdx - ddx
	ld	a, (c_ddx)
	ld	e, a
	ld	a, c
	sub	a, e
	ld	(c_dist), a
	ld	a, (c_ddx+1)
	ld	e, a
	ld	a, b
	sbc	a, e
	ld	(c_dist+1), a
	jr	220$
215$:
	; y step
	ld	a, (c_ddy)
	add	a, e
	ld	e, a
	ld	a, (c_ddy+1)
	adc	a, d
	ld	d, a
	push	de
	ld	a, (c_sty)
	ld	e, a
	ld	a, (c_sty+1)
	ld	d, a
	add	hl, de
	pop	de
	ld	a, (hl)
	bit	7, a
	jr	nz, 210$
	ld	(c_cell), a
	ld	a, #1
	ld	(c_side), a
	; dist = sdy - ddy
	ld	a, (c_ddy)
	ld	c, a
	ld	a, e
	sub	a, c
	ld	(c_dist), a
	ld	a, (c_ddy+1)
	ld	c, a
	ld	a, d
	sbc	a, c
	ld	(c_dist+1), a
220$:
	; perp = mulhi(dist, colcos[i])
	ld	a, (c_i)
	add	a, #<_T_COLCOS
	ld	l, a
	ld	a, #0
	adc	a, #>_T_COLCOS
	ld	h, a
	ld	b, (hl)
	ld	hl, #c_dist
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	call	mulhi
	; ray_d[i] = perp
	push	hl
	ld	a, (c_i)
	ld	e, a
	ld	d, #0
	ld	hl, #_ray_d
	add	hl, de
	add	hl, de
	pop	de
	ld	a, e
	ld	(hl+), a
	ld	(hl), d
	; idx = perp >> 2, clamp 2047
	ld	a, d
	cp	a, #0x20
	jr	c, 221$
	ld	de, #2047
	jr	222$
221$:
	srl	d
	rr	e
	srl	d
	rr	e
222$:
	ld	hl, #_T_HEIGHT
	add	hl, de
	ld	b, (hl)
	ld	a, (c_i)
	add	a, #<_ray_h
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_h
	ld	h, a
	ld	(hl), b
	; u
	ld	a, (c_side)
	or	a, a
	jr	nz, 230$
	; side 0: off from |sin| = T_COSABS[ay]; base fy; sign ypos; mirror if !xpos
	ld	hl, #c_ay
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	de, #_T_COSABS
	add	hl, de
	ld	b, (hl)
	ld	hl, #c_dist
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	call	mulhi
	ld	a, (c_ypos)
	or	a, a
	ld	a, (#_cam_y)
	jr	z, 231$
	add	a, l
	jr	232$
231$:
	sub	a, l
232$:
	swap	a
	and	a, #15
	ld	b, a
	ld	a, (c_xpos)
	or	a, a
	jr	nz, 240$
	ld	a, #15
	sub	a, b
	ld	b, a
	jr	240$
230$:
	; side 1: off from |cos| = T_COSABS[a]; base fx; sign xpos; mirror if ypos
	ld	hl, #c_a
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	de, #_T_COSABS
	add	hl, de
	ld	b, (hl)
	ld	hl, #c_dist
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	call	mulhi
	ld	a, (c_xpos)
	or	a, a
	ld	a, (#_cam_x)
	jr	z, 233$
	add	a, l
	jr	234$
233$:
	sub	a, l
234$:
	swap	a
	and	a, #15
	ld	b, a
	ld	a, (c_ypos)
	or	a, a
	jr	z, 240$
	ld	a, #15
	sub	a, b
	ld	b, a
240$:
	; ray_u[i] = b
	ld	a, (c_i)
	ld	c, a
	add	a, #<_ray_u
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_u
	ld	h, a
	ld	(hl), b
	; ray_t[i] = ((cell-1) << 1) | side
	ld	a, (c_cell)
	dec	a
	add	a, a
	ld	b, a
	ld	a, (c_side)
	or	a, b
	ld	b, a
	ld	a, c
	add	a, #<_ray_t
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_t
	ld	h, a
	ld	(hl), b
	ld	a, c
	inc	a
	ld	(c_i), a
	cp	a, #NRAYS
	jp	nz, 200$
	ret
