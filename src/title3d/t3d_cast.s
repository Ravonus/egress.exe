; EGRESS.EXE 3D renderer cast (SM83), from proto/raycast/v2/src/cast2.s: every ray's DDA. Changes for the game: the
; projection tables (T_DELTA, T_COSABS, T_HEIGHT, T_COLOFF, T_COLCOS) and the quarter squares live in the floors blob
; (___bank_t3d_bank_fl, addresses CT_* / SQPG from gen/t3d_layout.inc), which t3d_cast_asm maps first; the map is at
; T3D_MAP. The bank is left mapped for floors (t3d_render() in t3d_core.c restores the caller's bank).
	.include "t3d_layout.inc"
	.globl ___bank_t3d_bank_fl, __current_bank
	.area _CODE
; t3d_cast_asm: every ray's DDA (v1's, with 16 texel columns per face: u = frac >> 4)
	.globl _t3d_cast_init, _t3d_smulhi
	.globl _t3d_cast_asm, _t3d_cx, _t3d_cy, _t3d_ca, _t3d_ray_d

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

	.area _CODE

; hl = a * b  (quarter squares, page-aligned: lo bytes SQPG00-SQPG+1FF, hi bytes 0x200 above)
; in a, b; clobbers a, c, d, e
SQLO = SQPG
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

; DDA template, copied to WRAM by t3d_cast_init and patched per ray. in: bc = sdx, de = sdy, hl = cell; out: a = side
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
_t3d_smulhi::
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

_t3d_cast_init::
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

_t3d_cast_asm::
	ld	a, #<___bank_t3d_bank_fl
	ldh	(__current_bank), a
	ld	(0x2000), a
	ld	a, (#_t3d_cy+1)
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, (#_t3d_cx+1)
	add	a, l
	ld	l, a
	ld	de, #T3D_MAP
	add	hl, de
	ld	a, l
	ldh	(c_base), a
	ld	a, h
	ldh	(c_base+1), a
	xor	a, a
	ldh	(c_i), a
200$:
	; a = (t3d_ca + coloff[i]) & 1023
	ldh	a, (c_i)
	ld	l, a
	ld	h, #0
	add	hl, hl
	ld	de, #CT_COLOFF
	add	hl, de
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	a, (#_t3d_ca)
	add	a, l
	ld	l, a
	ldh	(c_a), a
	ldh	(c_ay), a
	ld	a, (#_t3d_ca+1)
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
	ld	de, #CT_DELTA
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
	ld	de, #CT_DELTA
	add	hl, de
	ld	a, (hl+)
	ldh	(c_ddy), a
	ld	a, (hl)
	ldh	(c_ddy+1), a
	; x side: stx, gx, sdx
	ldh	a, (c_xpos)
	or	a, a
	ld	a, (#_t3d_cx)
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
	ld	a, (#_t3d_cy)
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
	add	a, #<CT_COLCOS
	ld	l, a
	ld	a, #0
	adc	a, #>CT_COLCOS
	ld	h, a
	ld	b, (hl)
	ld	hl, #c_dist
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	MULHI
	; t3d_ray_d[i] = perp
	push	hl
	ldh	a, (c_i)
	ld	e, a
	ld	d, #0
	ld	hl, #_t3d_ray_d
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
	ld	hl, #CT_HEIGHT
	add	hl, de
	ld	b, (hl)
	ldh	a, (c_i)
	add	a, #<_t3d_ray_h
	ld	l, a
	ld	a, #0
	adc	a, #>_t3d_ray_h
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
	ld	de, #CT_COSABS
	add	hl, de
	ld	b, (hl)
	ld	hl, #c_dist
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	MULHI
	ldh	a, (c_ypos)
	or	a, a
	ld	a, (#_t3d_cy)
	jr	z, 231$
	add	a, l
	jr	232$
231$:
	sub	a, l
232$:
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
	ld	de, #CT_COSABS
	add	hl, de
	ld	b, (hl)
	ld	hl, #c_dist
	ld	a, (hl+)
	ld	d, (hl)
	ld	e, a
	MULHI
	ldh	a, (c_xpos)
	or	a, a
	ld	a, (#_t3d_cx)
	jr	z, 233$
	add	a, l
	jr	234$
233$:
	sub	a, l
234$:
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
	; t3d_ray_u[i] = b
	ldh	a, (c_i)
	ld	c, a
	add	a, #<_t3d_ray_u
	ld	l, a
	ld	a, #0
	adc	a, #>_t3d_ray_u
	ld	h, a
	ld	(hl), b
	; t3d_ray_t[i] = ((cell-1) << 1) | side
	ldh	a, (c_cell)
	dec	a
	add	a, a
	ld	b, a
	ldh	a, (c_side)
	or	a, b
	ld	b, a
	ld	a, c
	add	a, #<_t3d_ray_t
	ld	l, a
	ld	a, #0
	adc	a, #>_t3d_ray_t
	ld	h, a
	ld	(hl), b
	ld	a, c
	inc	a
	ldh	(c_i), a
	cp	a, #NRAYS
	jp	nz, 200$
	ret
