; cast_asm: every ray's DDA (v1's, with 16 texel columns per face: u = frac >> 4)
	.globl _cast_init, _smulhi
	.globl _cast_asm, _cam_x, _cam_y, _cam_a, _map, _ray_d, _T_COLOFF, _T_COLCOS, _T_DELTA, _T_COSABS
	.globl _T_HEIGHT, _ray_hc, _ray_hr, _ray_fr, _ray_mr, _ray_t, _ray_u, _ray_h

; cast scratch in HRAM (the walls/floors reuse it after the cast)
c_i = 0xFFA4
c_a = 0xFFA5
c_ay = 0xFFA7
c_xpos = 0xFFA9
c_ypos = 0xFFAA
c_ddx = 0xFFAB
c_ddy = 0xFFAD
c_stx = 0xFFAF
c_sty = 0xFFB1
c_dist = 0xFFB3
c_side = 0xFFB5
c_cell = 0xFFB6
c_base = 0xFFB7
c_col = 0xFFB9
c_row = 0xFFBA
c_fr = 0xFFBB

	.area _CODE

; hl = a * b  (quarter squares, page-aligned tables at 0x6C00: lo bytes 0x6C00-0x6DFF, hi bytes 0x6E00-0x6FFF)
; in a, b; clobbers a, c, d, e
SQLO = 0x6C
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

; DDA template, copied to WRAM by cast_init and patched per ray. in: bc = sdx, de = sdy, hl = cell; out: a = side
dda_tpl:
	ld	a, b
	cp	a, d
	jr	c, 1$
	jr	nz, 2$
	ld	a, c
	cp	a, e
	jr	nc, 2$
1$:
	ld	a, c
	add	a, #0
	ld	c, a
	ld	a, b
	adc	a, #0
	ld	b, a
	inc	hl
	bit	7, (hl)
	jr	nz, dda_tpl
	xor	a, a
	ret
2$:
	ld	a, e
	add	a, #0
	ld	e, a
	ld	a, d
	adc	a, #0
	ld	d, a
	ld	a, l
	add	a, #0
	ld	l, a
	ld	a, h
	adc	a, #0
	ld	h, a
	bit	7, (hl)
	jr	nz, dda_tpl
	ld	a, #1
	ret
dda_end:
DDA_N = dda_end - dda_tpl
DX_L = 12
DX_H = 16
XSTEP = 18
DY_L = 27
DY_H = 31
SY_L = 35
SY_H = 39

; inline forms: MUL8 = hl = a * b ; MULHI = hl = (de * b) >> 8 (clobbers a, c, d, e)
	.macro	MUL8	?l1
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
	jr	nc, l1
	cpl
	inc	a
l1:
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
	.endm

	.macro	MULHI	?l1
	ld	a, e
	ld	e, d
	push	de
	MUL8
	pop	de
	ld	a, e
	ld	e, h
	push	de
	MUL8
	pop	de
	ld	a, e
	add	a, l
	ld	l, a
	jr	nc, l1
	inc	h
l1:
	.endm

; int16_t smulhi(int16_t r /* de */, int16_t c /* bc, |c| <= 255 */) -> bc = (r * c) >> 8, rounded toward zero like
; core2.c's C version (magnitudes multiplied, sign applied after)
_smulhi::
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
	ld	a, c
	jr	z, 2$
	cpl
	inc	a
2$:
	ld	b, a
	MULHI
	pop	af
	bit	7, a
	jr	z, 3$
	xor	a, a
	sub	a, l
	ld	l, a
	ld	a, #0
	sbc	a, h
	ld	h, a
3$:
	ld	b, h
	ld	c, l
	ret

_cast_init::
	ld	hl, #dda_tpl
	ld	de, #dda_ram
	ld	b, #DDA_N
3$:
	ld	a, (hl+)
	ld	(de), a
	inc	de
	dec	b
	jr	nz, 3$
	ret

	.area _DATA
dda_ram:	.ds	48
	.area _CODE

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
	ldh	(c_base), a
	ld	a, h
	ldh	(c_base+1), a
	; the even rays and the last one in full, then each odd ray from its neighbours when they hit the same face
	xor	a, a
	ldh	(c_i), a
1$:
	call	cast1
	ldh	a, (c_i)
	add	a, #2
	ldh	(c_i), a
	cp	a, #NRAYS
	jr	c, 1$
	ld	a, #NRAYS - 1
	ldh	(c_i), a
	call	cast1
	ld	a, #1
	ldh	(c_i), a
2$:
.ifdef NOMID
	call	cast1
.else
	call	mid1
	call	nc, cast1
.endif
	ldh	a, (c_i)
	add	a, #2
	ldh	(c_i), a
	cp	a, #NRAYS - 1
	jr	c, 2$
	ret

; one full ray (c_i), with its hit record (ray_hc, ray_hr, ray_fr, ray_mr) for mid1
cast1:
	; a = (cam_a + coloff[i]) & 1023
	ldh	a, (c_i)
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
	ldh	(c_a), a
	ldh	(c_ay), a
	ld	a, (#_cam_a+1)
	adc	a, h
	and	a, #3
	ld	h, a
	ldh	(c_a+1), a
	; ay = a - 256
	dec	a
	and	a, #3
	ldh	(c_ay+1), a
	; xpos = ((h + 1) & 2) == 0 ; ypos = (h & 2) == 0
	ld	a, h
	inc	a
	and	a, #2
	ld	a, #0
	jr	nz, 201$
	inc	a
201$:
	ldh	(c_xpos), a
	ld	a, h
	and	a, #2
	ld	a, #0
	jr	nz, 202$
	inc	a
202$:
	ldh	(c_ypos), a
	; ddx = T_DELTA[a]
	add	hl, hl
	ld	de, #_T_DELTA
	add	hl, de
	ld	a, (hl+)
	ldh	(c_ddx), a
	ld	a, (hl)
	ldh	(c_ddx+1), a
	; ddy = T_DELTA[ay]
	ld	hl, #c_ay
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	add	hl, hl
	ld	de, #_T_DELTA
	add	hl, de
	ld	a, (hl+)
	ldh	(c_ddy), a
	ld	a, (hl)
	ldh	(c_ddy+1), a
	; x side: stx, gx, sdx
	ldh	a, (c_xpos)
	or	a, a
	ld	a, (#_cam_x)
	jr	z, 203$
	cpl
	ld	b, a
	ld	a, #1
	ldh	(c_stx), a
	xor	a, a
	ldh	(c_stx+1), a
	jr	204$
203$:
	ld	b, a
	ld	a, #0xFF
	ldh	(c_stx), a
	ldh	(c_stx+1), a
204$:
	ld	hl, #c_ddx
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	MULHI
	push	hl			; sdx
	; y side: sty, gy, sdy
	ldh	a, (c_ypos)
	or	a, a
	ld	a, (#_cam_y)
	jr	z, 205$
	cpl
	ld	b, a
	ld	a, #32
	ldh	(c_sty), a
	xor	a, a
	ldh	(c_sty+1), a
	jr	206$
205$:
	ld	b, a
	ld	a, #0xE0
	ldh	(c_sty), a
	ld	a, #0xFF
	ldh	(c_sty+1), a
206$:
	ld	hl, #c_ddy
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	MULHI
	ld	d, h
	ld	e, l			; de = sdy
	pop	bc			; bc = sdx
	; patch the RAM DDA's immediates for this ray
	ldh	a, (c_ddx)
	ld	(dda_ram + DX_L), a
	ldh	a, (c_ddx+1)
	ld	(dda_ram + DX_H), a
	ldh	a, (c_ddy)
	ld	(dda_ram + DY_L), a
	ldh	a, (c_ddy+1)
	ld	(dda_ram + DY_H), a
	ldh	a, (c_xpos)
	or	a, a
	ld	a, #0x23		; inc hl
	jr	nz, 207$
	ld	a, #0x2B		; dec hl
207$:
	ld	(dda_ram + XSTEP), a
	ldh	a, (c_sty)
	ld	(dda_ram + SY_L), a
	ldh	a, (c_sty+1)
	ld	(dda_ram + SY_H), a
	ld	hl, #c_base
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	call	dda_ram
	ldh	(c_side), a
	ld	a, (hl)
	ldh	(c_cell), a
	; hit cell column / row (map at a 1 KB boundary: row = (h & 3) << 3 | l >> 5)
	ld	a, l
	and	a, #31
	ldh	(c_col), a
	push	bc			; bc = sd x (dist below)
	ld	a, h
	and	a, #3
	ld	b, a
	ld	a, l
	swap	a
	rrca
	and	a, #7
	ld	c, a
	ld	a, b
	add	a, a
	add	a, a
	add	a, a
	or	a, c
	ldh	(c_row), a
	pop	bc
	; dist = sd - dd of the side that hit
	ldh	a, (c_side)
	or	a, a
	jr	nz, 215$
	ldh	a, (c_ddx)
	ld	e, a
	ld	a, c
	sub	a, e
	ldh	(c_dist), a
	ldh	a, (c_ddx+1)
	ld	e, a
	ld	a, b
	sbc	a, e
	ldh	(c_dist+1), a
	jr	220$
215$:
	ldh	a, (c_ddy)
	ld	c, a
	ld	a, e
	sub	a, c
	ldh	(c_dist), a
	ldh	a, (c_ddy+1)
	ld	c, a
	ld	a, d
	sbc	a, c
	ldh	(c_dist+1), a
220$:
	; perp = mulhi(dist, colcos[i])
	ldh	a, (c_i)
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
	MULHI
	; ray_d[i] = perp
	push	hl
	ldh	a, (c_i)
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
	ldh	a, (c_i)
	add	a, #<_ray_h
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_h
	ld	h, a
	ld	(hl), b
	; u
	ldh	a, (c_side)
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
	MULHI
	ldh	a, (c_ypos)
	or	a, a
	ld	a, (#_cam_y)
	jr	z, 231$
	add	a, l
	jr	232$
231$:
	sub	a, l
232$:
	ldh	(c_fr), a
	swap	a
	and	a, #15
	ld	b, a
	ldh	a, (c_xpos)
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
	MULHI
	ldh	a, (c_xpos)
	or	a, a
	ld	a, (#_cam_x)
	jr	z, 233$
	add	a, l
	jr	234$
233$:
	sub	a, l
234$:
	ldh	(c_fr), a
	swap	a
	and	a, #15
	ld	b, a
	ldh	a, (c_ypos)
	or	a, a
	jr	z, 240$
	ld	a, #15
	sub	a, b
	ld	b, a
240$:
	; ray_u[i] = b
	ldh	a, (c_i)
	ld	c, a
	add	a, #<_ray_u
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_u
	ld	h, a
	ld	(hl), b
	; ray_t[i] = ((cell-1) << 1) | side
	ldh	a, (c_cell)
	dec	a
	add	a, a
	ld	b, a
	ldh	a, (c_side)
	or	a, b
	ld	b, a
	ld	a, c
	add	a, #<_ray_t
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_t
	ld	h, a
	ld	(hl), b
	; hit record
	ld	b, #0
	ld	hl, #_ray_hc
	add	hl, bc
	ldh	a, (c_col)
	ld	(hl), a
	ld	hl, #_ray_hr
	add	hl, bc
	ldh	a, (c_row)
	ld	(hl), a
	ld	hl, #_ray_fr
	add	hl, bc
	ldh	a, (c_fr)
	ld	(hl), a
	; mirror: side 0 when !xpos, side 1 when ypos
	ldh	a, (c_side)
	or	a, a
	jr	nz, 250$
	ldh	a, (c_xpos)
	xor	a, #1
	jr	251$
250$:
	ldh	a, (c_ypos)
251$:
	ld	hl, #_ray_mr
	add	hl, bc
	ld	(hl), a
	ret

; ---------------------------------------------------------------------------------------------------------------
; mid1: ray c_i from rays c_i - 1 and c_i + 1 (core2.c mid_ray, same arithmetic); carry set when done
m_side = 0xFFA5
m_h0 = 0xFFA6
m_h1 = 0xFFA7
m_fix = 0xFFA8
m_la = 0xFFA9
m_lb = 0xFFAA
m_fr0 = 0xFFAB
m_fr1 = 0xFFAC
m_neg = 0xFFAD
RTAB = 0x70
HIXP = 0x6B
mid1:
	ldh	a, (c_i)
	dec	a
	ld	e, a
	ld	d, #0
	ld	hl, #_ray_t
	add	hl, de
	ld	a, (hl+)
	inc	hl
	xor	a, (hl)
	and	a, #1
	jp	nz, 90$
	ld	a, (hl)
	and	a, #1
	ldh	(m_side), a
	ld	hl, #_ray_h
	add	hl, de
	ld	a, (hl+)
	ldh	(m_h0), a
	inc	a
	jp	z, 90$
	inc	hl
	ld	a, (hl)
	ldh	(m_h1), a
	inc	a
	jp	z, 90$
	ldh	a, (m_side)
	or	a, a
	jr	nz, 10$
	; side 0: same column; la, lb = rows
	ld	hl, #_ray_hc
	add	hl, de
	ld	a, (hl+)
	inc	hl
	cp	a, (hl)
	jp	nz, 90$
	ldh	(m_fix), a
	ld	hl, #_ray_hr
	jr	11$
10$:
	ld	hl, #_ray_hr
	add	hl, de
	ld	a, (hl+)
	inc	hl
	cp	a, (hl)
	jp	nz, 90$
	ldh	(m_fix), a
	ld	hl, #_ray_hc
11$:
	add	hl, de
	ld	a, (hl+)
	ldh	(m_la), a
	ld	b, a
	inc	hl
	ld	a, (hl)
	ldh	(m_lb), a
	sub	a, b
	inc	a
	cp	a, #3
	jp	nc, 90$
	ld	hl, #_ray_fr
	add	hl, de
	ld	a, (hl+)
	ldh	(m_fr0), a
	inc	hl
	ld	a, (hl)
	ldh	(m_fr1), a
	; r = RT[HIX[h0] << 6 | HIX[h1]]
	ld	h, #HIXP
	ldh	a, (m_h1)
	ld	l, a
	ld	c, (hl)
	ldh	a, (m_h0)
	ld	l, a
	ld	l, (hl)
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, l
	or	a, c
	ld	l, a
	ld	a, h
	add	a, #RTAB
	ld	h, a
	ld	b, (hl)
	; dw = w1 - w0, |dw| into de, sign into m_neg
	ldh	a, (m_fr0)
	ld	c, a
	ldh	a, (m_fr1)
	sub	a, c
	ld	e, a
	ldh	a, (m_la)
	ld	c, a
	ldh	a, (m_lb)
	sbc	a, c
	ld	d, a
	ld	a, #0
	jr	nc, 20$
	xor	a, a
	sub	a, e
	ld	e, a
	ld	a, #0
	sbc	a, d
	ld	d, a
	ld	a, #1
20$:
	ldh	(m_neg), a
	push	bc
	MULHI
	pop	bc
	; wm = w0 +- hl -> d = row/col along, e = frac
	ldh	a, (m_neg)
	or	a, a
	jr	nz, 21$
	ldh	a, (m_fr0)
	add	a, l
	ld	e, a
	ldh	a, (m_la)
	adc	a, h
	ld	d, a
	jr	22$
21$:
	ldh	a, (m_fr0)
	sub	a, l
	ld	e, a
	ldh	a, (m_la)
	sbc	a, h
	ld	d, a
22$:
	; cell = side 0: (along & 31) << 5 | fix ; side 1: fix << 5 | (along & 31)
	ldh	a, (m_side)
	or	a, a
	ld	a, d
	jr	nz, 23$
	and	a, #31
	ld	l, a
	ldh	a, (m_fix)
	ld	c, a
	jr	24$
23$:
	and	a, #31
	ld	c, a
	ldh	a, (m_fix)
	ld	l, a
24$:
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, l
	or	a, c
	ld	l, a
	ld	a, h
	add	a, #>_map
	ld	h, a
	ld	a, (hl)
	bit	7, a
	jp	nz, 90$
	; ray_t[i] = ((c - 1) << 1) | side
	dec	a
	add	a, a
	ld	b, a
	ldh	a, (m_side)
	or	a, b
	ld	b, a
	ldh	a, (c_i)
	ld	c, a
	ld	a, b
	ld	b, #0
	ld	hl, #_ray_t
	add	hl, bc
	ld	(hl), a
	; u = frac >> 4, mirrored like ray i - 1
	ld	a, e
	swap	a
	and	a, #15
	ld	d, a
	ld	hl, #_ray_mr - 1
	add	hl, bc
	ld	a, (hl)
	or	a, a
	ld	a, d
	jr	z, 25$
	cpl
	and	a, #15
25$:
	ld	hl, #_ray_u
	add	hl, bc
	ld	(hl), a
	; ray_h[i] = (h0 + h1) >> 1
	ldh	a, (m_h0)
	ld	d, a
	ldh	a, (m_h1)
	add	a, d
	rra
	ld	hl, #_ray_h
	add	hl, bc
	ld	(hl), a
	; ray_d[i] = (d0 + d1) >> 1 (17-bit sum)
	ld	hl, #_ray_d - 2
	add	hl, bc
	add	hl, bc
	ld	a, (hl+)
	ld	e, a
	ld	a, (hl+)
	ld	d, a
	inc	hl
	inc	hl
	ld	a, (hl+)
	add	a, e
	ld	e, a
	ld	a, (hl)
	adc	a, d
	rra
	ld	d, a
	ld	a, e
	rra
	dec	hl
	dec	hl
	dec	hl
	ld	(hl+), a
	ld	(hl), d
	scf
	ret
90$:
	or	a, a
	ret
