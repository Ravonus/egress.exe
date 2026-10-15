; Raycaster run writers + column filler (SM83). Assembled after a generated cfg.s that defines VOFF, VEND, STRIP,
; RPT, NRAYS, DITH_H and the MASKS table for the build's resolution and column width.
;
; Run writers write `b` rows (>= 1) at hl; each row is two bytes (plane 0, plane 1) of one tile row, consecutive.
;   rs_set   d=lo e=hi                 rs_or    (hl) |= d / e
;   rs_set_d first row d/e, then d ^= c, e ^= (_run_hi1) per row  (dithered)      rs_or_d  same, or-ing
;   rs_zero  zeros                     rs_mask  (hl) = (hl) & c | d / e
; The C-callable _run_* wrappers take their inputs from the _run_* globals (used by the C sprite path).

	.module run_asm
	.globl _run_p, _run_n, _run_lo0, _run_hi0, _run_lo1, _run_hi1, _run_keep
	.globl _run_set, _run_or, _run_set_d, _run_or_d, _run_zero, _run_mask
	.globl _fill_asm, _fill_buf
	.globl _ray_h, _ray_t, _ray_u, _T_VTOP, _T_HV, _T_RUNS

	.area _DATA
_fill_buf:	.ds 2
f_strip:	.ds 2
f_i:		.ds 1
f_slot:		.ds 1
f_m:		.ds 1
f_vt:		.ds 1
f_dith:		.ds 1
f_hv:		.ds 2
f_rn:		.ds 2
f_r:		.ds 1
f_vend:		.ds 1
f_c:		.ds 1
f_tlo:		.ds 1
f_h:		.ds 1
f_mode:		.ds 1
f_p:		.ds 2

	.area _CODE

setup:
	ld	hl, #_run_n
	ld	b, (hl)
	ld	hl, #_run_lo0
	ld	d, (hl)
	ld	hl, #_run_hi0
	ld	e, (hl)
	ld	a, (#_run_lo1)
	ld	c, a
	ld	hl, #_run_p
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ret

_run_set::
	call	setup
	jr	rs_set
_run_zero::
	call	setup
	jr	rs_zero
_run_or::
	call	setup
	jr	rs_or
_run_set_d::
	call	setup
	jr	rs_set_d
_run_or_d::
	call	setup
	jr	rs_or_d
_run_mask::
	call	setup
	ld	a, (#_run_keep)
	ld	c, a
	jr	rs_mask

rs_set:
	srl	b
	jr	nc, 1$
	ld	a, d
	ld	(hl+), a
	ld	a, e
	ld	(hl+), a
	inc	b
	dec	b
	ret	z
1$:
	ld	a, d
	ld	(hl+), a
	ld	a, e
	ld	(hl+), a
	ld	a, d
	ld	(hl+), a
	ld	a, e
	ld	(hl+), a
	dec	b
	jr	nz, 1$
	ret

rs_zero:
	xor	a, a
	srl	b
	jr	nc, 1$
	ld	(hl+), a
	ld	(hl+), a
	inc	b
	dec	b
	ret	z
1$:
	ld	(hl+), a
	ld	(hl+), a
	ld	(hl+), a
	ld	(hl+), a
	dec	b
	jr	nz, 1$
	ret

rs_or:
	ld	a, (hl)
	or	a, d
	ld	(hl+), a
	ld	a, (hl)
	or	a, e
	ld	(hl+), a
	dec	b
	jr	nz, rs_or
	ret

rs_set_d:
	ld	a, d
	ld	(hl+), a
	xor	a, c
	ld	d, a
	ld	a, e
	ld	(hl+), a
	ld	a, (#_run_hi1)
	xor	a, e
	ld	e, a
	dec	b
	jr	nz, rs_set_d
	ret

rs_or_d:
	ld	a, (hl)
	or	a, d
	ld	(hl+), a
	ld	a, d
	xor	a, c
	ld	d, a
	ld	a, (hl)
	or	a, e
	ld	(hl+), a
	ld	a, (#_run_hi1)
	xor	a, e
	ld	e, a
	dec	b
	jr	nz, rs_or_d
	ret

rs_mask:
	ld	a, (hl)
	and	a, c
	or	a, d
	ld	(hl+), a
	ld	a, (hl)
	and	a, c
	or	a, e
	ld	(hl+), a
	dec	b
	jr	nz, rs_mask
	ret

; hl = f_strip + a*2  (a = row 0..VIEW_H-1)
strip_row:
	add	a, a
	ld	hl, #f_strip
	add	a, (hl)
	inc	hl
	ld	h, (hl)
	ld	l, a
	ret	nc
	inc	h
	ret

; fill_asm: the whole framebuffer from ray_h / ray_t / ray_u (same output as fill_all in core.c)
; HV2 = page-aligned T_HV transposed: HV2[vend][h] at (HV2PAGE + vend) * 256 + h
HV2PAGE = 0x68
_fill_asm::
	ld	hl, #_fill_buf
	ld	a, (hl+)
	ld	(f_strip), a
	ld	a, (hl)
	ld	(f_strip+1), a
	xor	a, a
	ld	(f_i), a
100$:
	ld	a, (f_i)
	ld	c, a
	and	a, #(RPT-1)
	ld	(f_slot), a
	jr	nz, 101$
	ld	a, c
	or	a, a
	jr	z, 101$
	ld	hl, #f_strip
	ld	a, (hl)
	add	a, #<STRIP
	ld	(hl+), a
	ld	a, (hl)
	adc	a, #>STRIP
	ld	(hl), a
101$:
	; m = MASKS[slot]
	ld	a, (f_slot)
	add	a, #<MASKS
	ld	l, a
	ld	a, #0
	adc	a, #>MASKS
	ld	h, a
	ld	a, (hl)
	ld	(f_m), a
	; h = ray_h[i]
	ld	a, c
	add	a, #<_ray_h
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_h
	ld	h, a
	ld	b, (hl)
	ld	a, b
	ld	(f_h), a
	; mode = slot ? 1 : 0, +2 when far (dithered)
	cp	a, #DITH_H
	ld	a, #0
	jr	nc, 102$
	ld	a, #2
102$:
	ld	e, a
	ld	a, (f_slot)
	or	a, a
	jr	z, 103$
	inc	e
103$:
	ld	a, e
	ld	(f_mode), a
	; vt = T_VTOP[h]
	ld	a, b
	add	a, #<_T_VTOP
	ld	l, a
	ld	a, #0
	adc	a, #>_T_VTOP
	ld	h, a
	ld	a, (hl)
	ld	(f_vt), a
	; r0 = max(vt, VOFF)
	cp	a, #VOFF
	jr	nc, 104$
	ld	a, #VOFF
104$:
	ld	(f_r), a
	; de = T_RUNS + (t*8+u)*32
	ld	a, c
	add	a, #<_ray_t
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_t
	ld	h, a
	ld	a, (hl)
	add	a, a
	add	a, a
	add	a, a
	ld	b, a
	ld	a, c
	add	a, #<_ray_u
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_u
	ld	h, a
	ld	a, (hl)
	add	a, b
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, #_T_RUNS
	add	hl, de
	ld	d, h
	ld	e, l
	; hl = row pointer at r0 (slot 0 zeroes the ceiling rows on the way)
	ld	hl, #f_strip
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	a, (f_r)
	sub	a, #VOFF
	jr	z, 106$
	ld	b, a
	ld	a, (f_slot)
	or	a, a
	jr	nz, 105$
	push	de
	call	rs_zero
	pop	de
	jr	106$
105$:
	ld	a, b
	add	a, a
	add	a, l
	ld	l, a
	jr	nc, 106$
	inc	h
106$:
	ld	a, l
	ld	(f_p), a
	ld	a, h
	ld	(f_p+1), a
110$:
	; next run: vend, colour; end = clamp(vt + HV2[vend][h])
	ld	a, (de)
	inc	de
	ld	(f_vend), a
	add	a, #HV2PAGE
	ld	h, a
	ld	a, (de)
	inc	de
	ld	c, a
	ld	a, (f_h)
	ld	l, a
	ld	a, (f_vt)
	add	a, (hl)
	cp	a, #VEND
	jr	c, 111$
	ld	a, #VEND
111$:
	cp	a, #VOFF
	jr	nc, 112$
	ld	a, #VOFF
112$:
	ld	hl, #f_r
	ld	b, a
	sub	a, (hl)
	ld	(hl), b
	jr	z, 139$
	ld	b, a
	push	de
	ld	a, (f_m)
	ld	d, a
	ld	e, a
	ld	a, c
	rra
	jr	c, 113$
	ld	d, #0
113$:
	rra
	jr	c, 114$
	ld	e, #0
114$:
	ld	hl, #f_p
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	a, (f_mode)
	or	a, a
	jr	nz, 115$
	call	rs_set
	jr	138$
115$:
	dec	a
	jr	nz, 116$
	call	rs_or
	jr	138$
116$:
	; dithered (mode 2 set / 3 or): first row masked by 0xAA (even) / 0x55 (odd), toggled by full bytes
	push	af
	ld	a, e
	ld	(_run_hi1), a
	ld	a, d
	ld	(f_tlo), a
	ld	a, (f_r)
	sub	a, b
	sub	a, #VOFF
	rra
	ld	a, #0xAA
	jr	nc, 117$
	ld	a, #0x55
117$:
	ld	c, a
	ld	a, d
	and	a, c
	ld	d, a
	ld	a, e
	and	a, c
	ld	e, a
	ld	a, (f_tlo)
	ld	c, a
	pop	af
	dec	a
	jr	nz, 118$
	call	rs_set_d
	jr	138$
118$:
	call	rs_or_d
138$:
	ld	a, l
	ld	(f_p), a
	ld	a, h
	ld	(f_p+1), a
	pop	de
139$:
	ld	a, (f_vend)
	cp	a, #15
	jp	nz, 110$
	; slot 0: zero the floor rows [r, VEND)
	ld	a, (f_slot)
	or	a, a
	jr	nz, 149$
	ld	a, (f_r)
	cp	a, #VEND
	jr	nc, 149$
	ld	c, a
	ld	a, #VEND
	sub	a, c
	ld	b, a
	ld	hl, #f_p
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	call	rs_zero
149$:
	ld	a, (f_i)
	inc	a
	ld	(f_i), a
	cp	a, #NRAYS
	jp	nz, 100$
	ret

; ---------------------------------------------------------------------------------------------------------------
; cast_asm: every ray's DDA (same results as cast_all in core.c)
	.globl _cast_asm, _cam_x, _cam_y, _cam_a, _map, _ray_d, _T_COLOFF, _T_COLCOS, _T_DELTA, _T_COSABS
	.globl _T_HEIGHT, _T_SQ

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
	or	a, a
	jr	z, 210$
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
	or	a, a
	jr	z, 210$
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
	rrca
	and	a, #7
	ld	b, a
	ld	a, (c_xpos)
	or	a, a
	jr	nz, 240$
	ld	a, #7
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
	rrca
	and	a, #7
	ld	b, a
	ld	a, (c_ypos)
	or	a, a
	jr	z, 240$
	ld	a, #7
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
