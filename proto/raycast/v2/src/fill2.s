; Raycaster v2 fill (SM83): walls_asm and floors_asm. Same output as walls_all / floors_all in core2.c (the host
; build of that C is diffed against the cartridge). Assembled after the generated cfg (VOFF, VEND, STRIP, NRAYS,
; VIEW_H, TW) and layout.inc (bank blob addresses: walls data in ROM bank 2, floor data in ROM bank 3).
;
; Write scheme (equivalent to the C's masked writes): slot 0 (left 4 px of a tile byte) stores its wall rows with
; the right nibble 0 and zeroes all its other rows; slot 1 and the floor/ceiling pass then only OR into nibbles that
; are known to be 0.

	.module fill2
	.globl _ray_h, _ray_t, _ray_u, _ray_fv, _ray_cv
	.globl _walls_asm, _floors_asm, _w_buf
	.globl _f_lx, _f_ly, _f_ux, _f_uy, _f_cx8, _f_cy8, _f_minfv
	.globl __current_bank
	.globl _map, _cam_a
.if OBJ_ON
	.globl _vis_fill_asm, _vf_j0, _vf_n, _vf_d, _vpad, _ray_d
	.globl _bb_col, _bbe_y8, _bbe_nr, _bbe_ox, _bbe_attr, _obj_oam, _obj_on, _obj_stage_n
.endif
	.globl _bbc_src, _bbc_mask, _bbc_bank
.if OBJ_ON
	.globl _oam_dma
.endif
_map = MAPADDR

	.area _DATA
_w_buf:	.ds 2
_f_lx:	.ds 1
_f_ly:	.ds 1
_f_ux:	.ds 1
_f_uy:	.ds 1
_f_cx8:	.ds 2
_f_cy8:	.ds 2
_f_minfv:	.ds 1
_bbc_src:	.ds 2
_bbc_mask:	.ds 1
_bbc_bank:	.ds 1
_bbe_y8:	.ds 1
_vf_j0:	.ds 1
_vf_n:	.ds 1
_vf_d:	.ds 2
_bbe_nr:	.ds 1
_bbe_ox:	.ds 1
_bbe_attr:	.ds 1

; HRAM scratch (GBDK uses 0xFF80-0xFF92)
HPAT = 0xA0		; 4 pattern bytes 0xFFA0-0xFFA3 (cycled with inc c / res 2, c)
W_I = 0xFFA4
W_SLOT = 0xFFA5
W_M = 0xFFA6
W_H = 0xFFA7
W_VT = 0xFFA8
W_K = 0xFFA9
W_SIDE = 0xFFAA
W_R = 0xFFAB
W_VEND = 0xFFAC
W_RNL = 0xFFAD
W_RNH = 0xFFAE
W_STL = 0xFFAF
W_STH = 0xFFB0
W_PATK = 0xFFB1
W_C = 0xFFB2
W_HT = 0xFFB3
W_ROW = 0xFFB4
W_T = 0xFFB5
W_CTK = 0xFFB6
W_PM = 0xFFB7
W_E1 = 0xFFF8
W_CTK1 = 0xFFF9
W_BOT = 0xFFFA
F_N = 0xFFFB
W_BK = 0xFFFC		; baked walls: bank, address high byte
W_AH = 0xFFFD

F_J = 0xFFB8
F_Y0 = 0xFFB9
F_YL = 0xFFBA
F_D = 0xFFBB
F_SXL = 0xFFBC
F_SXH = 0xFFBD
F_SYL = 0xFFBE
F_SYH = 0xFFBF
F_S2XL = 0xFFC0
F_S2XH = 0xFFC1
F_S2YL = 0xFFC2
F_S2YH = 0xFFC3
F_T = 0xFFC4
F_STL = 0xFFC5
F_STH = 0xFFC6
F_B0 = 0xFFC7
FMIN = 0xC8		; HRAM 0xFFC8: per tile column, first row with any floor (C fills it)
CMAX = FMIN + TW	; HRAM: per tile column, first row with all floor + ceiling visible
F_B1 = 0xFFF6
PF0 = 0xFF93
PF1 = 0xFF94
PF2 = 0xFF95
PF3 = 0xFF96
PC0 = 0xFF9B
PC1 = 0xFF9C
PC2 = 0xFF9D
PC3 = 0xFF9E
; PF0-7 / PC0-7: floor / ceiling row fog patterns, 0xFF93-0xFFA2 (HPAT's bytes, free during floors)
PF4 = 0xFF97
PF5 = 0xFF98
PF6 = 0xFF99
PF7 = 0xFF9A
PC4 = 0xFF9F
PC5 = 0xFFA0
PC6 = 0xFFA1
PC7 = 0xFFA2
NOFOG = 0xFFF1
P_FL = 0xFFF2		; partial path: floor lo, hi, ceiling lo, hi
P_CL = 0xFFF4
; partial-path temps reuse the walls' HRAM
F_FV0 = 0xFFA4
F_FV1 = 0xFFA5
F_CV0 = 0xFFA6
F_CV1 = 0xFFA7
F_K = 0xFFA8
F_VIS = 0xFFA9
F_YS = 0xFFAA		; partial path: first row of a slot-0 run
F_NST = 0xFFAB		; partial path: slot-0 rows stored outright
REC = 0xCB00		; per tile column {fv0, fv1, cv0, cv1} (C fills it)

	.area _CODE

; ---------------------------------------------------------------------------------------------------------------
; row writers: b rows (>= 1) at hl, two bytes per row
;   ws_set d/e        ws_or (hl) |= d/e        ws_zero
;   wd_set / wd_or    first row d/e, then d ^= c, e ^= (W_HT) per row (the 50% checker)
;   wp_set / wp_or    per row: (0xFF00 + c) pattern & d / e, c cycles 0xA0..0xA3
ws_set:
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

ws_zero:
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

ws_or:
	srl	b
	jr	nc, 1$
	ld	a, (hl)
	or	a, d
	ld	(hl+), a
	ld	a, (hl)
	or	a, e
	ld	(hl+), a
	inc	b
	dec	b
	ret	z
1$:
	ld	a, (hl)
	or	a, d
	ld	(hl+), a
	ld	a, (hl)
	or	a, e
	ld	(hl+), a
	ld	a, (hl)
	or	a, d
	ld	(hl+), a
	ld	a, (hl)
	or	a, e
	ld	(hl+), a
	dec	b
	jr	nz, 1$
	ret

; checker writers, two rows per pass: row A = d/e, row B = c/(W_E1) (c = d ^ lo-toggle, W_E1 = e ^ (W_HT))
	.macro	WD_SWAP
	ld	a, d
	ld	d, c
	ld	c, a
	ld	a, e
	push	af
	ldh	a, (W_E1)
	ld	e, a
	pop	af
	ldh	(W_E1), a
	inc	b
	dec	b
	ret	z
	.endm

wd_set:
	ld	a, d
	xor	a, c
	ld	c, a
	ldh	a, (W_HT)
	xor	a, e
	ldh	(W_E1), a
	srl	b
	jr	nc, 1$
	ld	a, d
	ld	(hl+), a
	ld	a, e
	ld	(hl+), a
	WD_SWAP
1$:
	ld	a, d
	ld	(hl+), a
	ld	a, e
	ld	(hl+), a
	ld	a, c
	ld	(hl+), a
	ldh	a, (W_E1)
	ld	(hl+), a
	dec	b
	jr	nz, 1$
	ret

wd_or:
	ld	a, d
	xor	a, c
	ld	c, a
	ldh	a, (W_HT)
	xor	a, e
	ldh	(W_E1), a
	srl	b
	jr	nc, 1$
	ld	a, (hl)
	or	a, d
	ld	(hl+), a
	ld	a, (hl)
	or	a, e
	ld	(hl+), a
	WD_SWAP
1$:
	ld	a, (hl)
	or	a, d
	ld	(hl+), a
	ld	a, (hl)
	or	a, e
	ld	(hl+), a
	ld	a, (hl)
	or	a, c
	ld	(hl+), a
	ldh	a, (W_E1)
	or	a, (hl)
	ld	(hl+), a
	dec	b
	jr	nz, 1$
	ret

wp_set:
	ldh	a, (c)
	and	a, d
	ld	(hl+), a
	ldh	a, (c)
	and	a, e
	ld	(hl+), a
	inc	c
	res	2, c
	dec	b
	jr	nz, wp_set
	ret

wp_or:
	ldh	a, (c)
	and	a, d
	or	a, (hl)
	ld	(hl+), a
	ldh	a, (c)
	and	a, e
	or	a, (hl)
	ld	(hl+), a
	inc	c
	res	2, c
	dec	b
	jr	nz, wp_or
	ret

; ---------------------------------------------------------------------------------------------------------------
; walls_asm: every ray column's textured, fogged wall run list (ROM bank 2 mapped while it runs).
; Per column: a 4-colour table (mode, lo, hi) is copied from CTAB into HRAM (skipped when the same as the previous
; column's); runs then write contiguously down the strip, so the dest pointer only moves with the writers.
; modes: 0 plain, 1 checker (50%), 2 Bayer pattern (HRAM 0xFFA0-3), 3 zero (slot 0) / skip (slot 1)
CTH0 = 0xB8			; HRAM 0xFFB8 / 0xFFC8: per slot 4 x [lo, hi, mode, -] (floors' HRAM, free here)
CTH1 = 0xC8

	.macro	RUNLOOP	slot, w_set, w_d, w_p
1$:
	ld	a, (de)
	inc	de
	add	a, #>HV2
	ld	b, a
	ldh	a, (W_H)
	ld	c, a
	ld	a, (bc)
	ld	c, a
	ldh	a, (W_VT)
	add	a, c
	cp	a, #VEND
	jr	c, 2$
	ld	a, #VEND
2$:
	cp	a, #VOFF
	jr	nc, 3$
	ld	a, #VOFF
3$:
	ld	c, a
	ldh	a, (W_R)
	ld	b, a
	ld	a, c
	ldh	(W_R), a
	sub	a, b
	ld	b, a
	ld	a, (de)
	inc	de
	jr	z, 9$
	add	a, a
	add	a, a
.if slot
	add	a, #CTH1
.else
	add	a, #CTH0
.endif
	ld	c, a
	push	de
	ldh	a, (c)
	ld	d, a
	inc	c
	ldh	a, (c)
	ld	e, a
	inc	c
	ldh	a, (c)
	or	a, a
	jr	nz, 4$
	call	w_set
	jr	8$
4$:
	dec	a
	jr	nz, 5$
	; checker: c = full lo, W_HT = full hi, first row masked 0x55 (even row) / 0xAA (odd)
	ld	a, e
	ldh	(W_HT), a
	ld	c, d
	ld	a, #0x55
	bit	1, l
	jr	z, 41$
	cpl
41$:
	ldh	(W_PM), a
	and	a, d
	ld	d, a
	ldh	a, (W_PM)
	and	a, e
	ld	e, a
	call	w_d
	jr	8$
5$:
	dec	a
	jr	nz, 6$
	ld	a, l
	rra
	and	a, #3
	or	a, #HPAT
	ld	c, a
	call	w_p
	jr	8$
6$:
	dec	a
	jr	z, 61$
	; mode 4: fixed pattern (HRAM 0xFFF0-3)
	ld	a, l
	rra
	and	a, #3
	or	a, #0xF0
	ld	c, a
	call	w_p
	jr	8$
61$:
.if slot
	ld	a, b
	add	a, a
	add	a, l
	ld	l, a
.else
	call	ws_zero
.endif
8$:
	pop	de
9$:
	ldh	a, (W_BOT)
	ld	c, a
	ldh	a, (W_R)
	cp	a, c
	jp	nz, 1$
	.endm

_walls_asm::
	ld	a, #2
	ldh	(__current_bank), a
	ld	(0x2000), a
	ld	hl, #_w_buf
	ld	a, (hl+)
	ldh	(W_STL), a
	ld	a, (hl)
	ldh	(W_STH), a
	; y-face floor pattern (level SIDEK) into HRAM 0xFFF0-3 (the floors' row-pattern slots, free here)
	ld	hl, #FOGPAT + SIDEK * 4
	ld	a, (hl+)
	ldh	(0xFFF0), a
	ld	a, (hl+)
	ldh	(0xFFF1), a
	ld	a, (hl+)
	ldh	(0xFFF2), a
	ld	a, (hl)
	ldh	(0xFFF3), a
	xor	a, a
	ldh	(W_I), a
	dec	a
	ldh	(W_PATK), a
	ldh	(W_CTK), a
	ldh	(W_CTK1), a
wcol:
	ldh	a, (W_I)
	ld	c, a
	ld	b, #0
	and	a, #1
	ldh	(W_SLOT), a
	jr	nz, 101$
	ld	a, c
	or	a, a
	jr	z, 101$
	ldh	a, (W_STL)
	add	a, #<STRIP
	ldh	(W_STL), a
	ldh	a, (W_STH)
	adc	a, #>STRIP
	ldh	(W_STH), a
101$:
	; h, vt, k; walls under MIPH px use the 8-row mip in bank 4
	ld	hl, #_ray_h
	add	hl, bc
	ld	a, (hl)
	ldh	(W_H), a
.if HB
	cp	a, #HB
	jp	c, wfast
.endif
	cp	a, #MIPH
	ld	a, #2
	jr	nc, 1011$
	ld	a, #4
1011$:
	ldh	(__current_bank), a
	ld	(0x2000), a
	ldh	a, (W_H)
	ld	e, a
	ld	l, a
	ld	h, #>VTOP
	ld	d, (hl)
	ld	a, d
	ldh	(W_VT), a
	ld	h, #>FOGK
	ld	a, (hl)
	ldh	(W_K), a
	; t
	ld	hl, #_ray_t
	add	hl, bc
	ld	a, (hl)
	ldh	(W_T), a
	; runs = *(WOFF + (t >> 1) * 32 + u * 2)
	push	de
	srl	a
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, c
	add	a, #<_ray_u
	ld	e, a
	ld	a, #0
	adc	a, #>_ray_u
	ld	d, a
	ld	a, (de)
	add	a, a
	add	a, l
	ld	l, a
	jr	nc, 102$
	inc	h
102$:
	ld	de, #WOFF
	add	hl, de
	ld	a, (hl+)
	ldh	(W_RNL), a
	ld	a, (hl)
	ldh	(W_RNH), a
	pop	de
	; ray_cv[i] = vt <= VOFF ? H : H + VOFF - vt
	ld	a, d
	cp	a, #VOFF + 1
	ld	a, #VIEW_H
	jr	c, 103$
	ld	a, #VIEW_H + VOFF
	sub	a, d
103$:
	ld	hl, #_ray_cv
	add	hl, bc
	ld	(hl), a
	; bot = vt + h ; ray_fv[i] = bot >= VEND ? H : bot - VOFF
	ld	a, d
	add	a, e
	ld	e, a
	cp	a, #VEND
	ld	a, #VIEW_H
	jr	nc, 104$
	ld	a, e
	sub	a, #VOFF
104$:
	push	af
	add	a, #VOFF
	ldh	(W_BOT), a
	pop	af
	ld	hl, #_ray_fv
	add	hl, bc
	ld	(hl), a
wz0:
	; slot 0 zeroes only the rows where ray i + 1 shows wall and ray i does not (floors OR into those; they store
	; the rest): [a1, min(vt, b1)) and [max(bot, a1), b1), a1 = max(vt1, VOFF), b1 = min(vt1 + h1, VEND)
	ldh	a, (W_SLOT)
	or	a, a
	jr	nz, 110$
	inc	c
	ld	hl, #_ray_h
	add	hl, bc
	dec	c
	ld	l, (hl)
	ld	h, #>VTOP
	ld	a, (hl)
	ld	h, a			; h = vt1
	add	a, l
	cp	a, #VEND
	jr	c, 1051$
	ld	a, #VEND
1051$:
	ld	l, a			; l = b1
	ld	a, h
	cp	a, #VOFF
	jr	nc, 1052$
	ld	a, #VOFF
1052$:
	ld	h, a			; h = a1
	; widened to the horizon band [H - ZBAND, ZBAND) (the floors skip its d = 0 rows; colour 0 there)
	cp	a, #VOFF + VIEW_H - ZBAND
	jr	c, 1056$
	ld	h, #VOFF + VIEW_H - ZBAND
1056$:
	ld	a, l
	cp	a, #VOFF + ZBAND
	jr	nc, 1057$
	ld	l, #VOFF + ZBAND
1057$:
	; top part: n = min(vt, b1) - a1
	ld	a, d
	cp	a, l
	jr	c, 1053$
	ld	a, l
1053$:
	sub	a, h
	jr	c, 1054$
	jr	z, 1054$
	push	hl
	ld	b, a
	ld	a, h
	sub	a, #VOFF
	add	a, a
	ld	c, a
	ldh	a, (W_STL)
	add	a, c
	ld	l, a
	ldh	a, (W_STH)
	ld	h, a
	call	ws_zero
	pop	hl
1054$:
	; bottom part: n = b1 - max(bot, a1)
	ld	a, e
	cp	a, h
	jr	nc, 1055$
	ld	a, h
1055$:
	ld	c, a
	ld	a, l
	sub	a, c
	jr	c, 110$
	jr	z, 110$
	ld	b, a
	ld	a, c
	sub	a, #VOFF
	add	a, a
	ld	c, a
	ldh	a, (W_STL)
	add	a, c
	ld	l, a
	ldh	a, (W_STH)
	ld	h, a
	call	ws_zero
110$:
.if HB
	ldh	a, (W_H)
	cp	a, #HB
	jp	c, wbake
.endif
	; colour table key = (side * 6 + kclass) * 16, cached per slot
	ldh	a, (W_K)
	add	a, #<KCLASS
	ld	l, a
	ld	h, #>KCLASS
	ld	c, (hl)
	ldh	a, (W_T)
	and	a, #1
	jr	z, 111$
	ld	a, #6
111$:
	add	a, c
	swap	a
	ld	b, a
	ldh	a, (W_SLOT)
	or	a, a
	jr	nz, 112$
	ldh	a, (W_CTK)
	cp	a, b
	jr	z, 113$
	ld	a, b
	ldh	(W_CTK), a
	ld	l, a
	ld	h, #>CTAB
	ld	c, #CTH0
	jr	1125$
112$:
	ldh	a, (W_CTK1)
	cp	a, b
	jr	z, 113$
	ld	a, b
	ldh	(W_CTK1), a
	ld	l, a
	ld	h, #>CTAB + 1
	ld	c, #CTH1
1125$:
	.rept	4
	ld	a, (hl+)
	ldh	(c), a
	inc	c
	ld	a, (hl+)
	ldh	(c), a
	inc	c
	ld	a, (hl+)
	ldh	(c), a
	inc	c
	inc	c
	inc	l
	.endm
113$:
	; Bayer pattern for levels 1-7 / 9-15
	ldh	a, (W_K)
	ld	c, a
	ldh	a, (W_PATK)
	cp	a, c
	jr	z, 115$
	ld	a, c
	ldh	(W_PATK), a
	add	a, a
	add	a, a
	ld	l, a
	ld	h, #>FOGPAT
	ld	a, (hl+)
	ldh	(0xFFA0), a
	ld	a, (hl+)
	ldh	(0xFFA1), a
	ld	a, (hl+)
	ldh	(0xFFA2), a
	ld	a, (hl)
	ldh	(0xFFA3), a
115$:
	; runs; skip the ones that end above the screen (tall walls); r = max(vt, VOFF), hl = strip + (r - VOFF) * 2
	ldh	a, (W_RNL)
	ld	e, a
	ldh	a, (W_RNH)
	ld	d, a
	ldh	a, (W_H)
	ld	l, a
	ld	h, #>V0T
	ld	b, (hl)
	ld	a, (de)
	cp	a, b
	jr	nc, 1159$
1158$:
	inc	de
	inc	de
	ld	a, (de)
	cp	a, b
	jr	c, 1158$
1159$:
	ldh	a, (W_VT)
	cp	a, #VOFF
	jr	nc, 116$
	ld	a, #VOFF
116$:
	ldh	(W_R), a
	sub	a, #VOFF
	add	a, a
	ld	c, a
	ldh	a, (W_STL)
	add	a, c
	ld	l, a
	ldh	a, (W_STH)
	ld	h, a
	ldh	a, (W_SLOT)
	or	a, a
	jp	nz, wl1
wl0:
	RUNLOOP	0, ws_set, wd_set, wp_set
	jp	wdone
wl1:
	RUNLOOP	1, ws_or, wd_or, wp_or
.if HB
	jp	wdone
; baked short wall (h < HB): its 2h slot-0 bytes from the pack_wb.py banks, stored (slot 0) or swapped and ORed
; (slot 1) by an unrolled copy entered at WBT's entry for h. The colour / pattern caches stay valid.
wfast:
	; h < HB: everything per height from WBT (h << 4), bank 2 for the zeroing's VTOP
	swap	a
	ld	l, a
	ld	h, #0
	ld	de, #_WBT
	add	hl, de
	ld	a, #2
	ldh	(__current_bank), a
	ld	(0x2000), a
	ld	a, (hl+)
	ldh	(W_BK), a
	ld	a, (hl+)
	ldh	(W_AH), a
	ld	a, (hl+)
	ldh	(W_C), a
	ld	a, (hl+)
	ldh	(W_ROW), a
	push	hl
	ld	a, (hl+)
	ld	hl, #_ray_cv
	add	hl, bc
	ld	(hl), a
	pop	hl
	inc	hl
	ld	a, (hl+)
	push	hl
	ld	hl, #_ray_fv
	add	hl, bc
	ld	(hl), a
	pop	hl
	ld	a, (hl+)
	ld	d, a
	ld	a, (hl+)
	ld	e, a
	ld	a, l
	ldh	(W_RNL), a
	ld	a, h
	ldh	(W_RNH), a
	ld	hl, #_ray_t
	add	hl, bc
	ld	a, (hl)
	ldh	(W_T), a
	jp	wz0
wbake:
	; the stored copy (slot 0) or swap + OR (slot 1) of the 2h baked bytes; the colour / pattern caches stay valid
	ldh	a, (W_C)
	or	a, a
	jp	z, wdone
	ld	b, a
	ldh	a, (W_RNL)
	ld	l, a
	ldh	a, (W_RNH)
	ld	h, a
	ldh	a, (W_SLOT)
	or	a, a
	jr	z, 1$
	inc	hl
	inc	hl
1$:
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ld	de, #wdone
	push	de			; the copy's ret goes to wdone
	push	hl			; the ret below enters the copy
	; source = ((t << 4 | u) << shift): bank W_BK + (src >> 14), address W_AH + (src & 0x3FFF)
	ldh	a, (W_I)
	add	a, #<_ray_u
	ld	l, a
	ld	a, #0
	adc	a, #>_ray_u
	ld	h, a
	ld	c, (hl)
	ldh	a, (W_T)
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, l
	or	a, c
	ld	l, a
2$:
	add	hl, hl
	dec	b
	jr	nz, 2$
	ld	a, h
	rlca
	rlca
	and	a, #3
	ld	c, a
	ldh	a, (W_BK)
	add	a, c
	ldh	(__current_bank), a
	ld	(0x2000), a
	ld	a, h
	and	a, #0x3F
	ld	c, a
	ldh	a, (W_AH)
	add	a, c
	ld	d, a
	ld	e, l
	; dest = strip + (vt - VOFF) * 2
	ldh	a, (W_ROW)
	ld	c, a
	ldh	a, (W_STL)
	add	a, c
	ld	l, a
	ldh	a, (W_STH)
	ld	h, a
	ret
; the unrolled copies: 2 (HB - 1) bytes, entered 2h bytes before the end
wcp0:
	.rept	2 * (HB - 1)
	ld	a, (de)
	inc	e
	ld	(hl+), a
	.endm
wcp0_end::
	ret
wcp1:
	.rept	2 * (HB - 1)
	ld	a, (de)
	inc	e
	swap	a
	or	a, (hl)
	ld	(hl+), a
	.endm
wcp1_end::
	ret
.endif
wdone:
	ldh	a, (W_I)
	inc	a
	ldh	(W_I), a
	cp	a, #NRAYS
	jp	nz, wcol
	ld	a, #1
	ldh	(__current_bank), a
	ld	(0x2000), a
	ret

; ---------------------------------------------------------------------------------------------------------------
; floors_asm: floor + ceiling, row-major (ROM bank 3 mapped: quarter squares, sets, row tables).
; Per sample row: world point of ray 0 and the per-ray step; per tile column: skip (FMIN), the fast path when both
; rays show floor and ceiling on every row of the group (CMAX), else the per-row masked path.

; hl = a * b (unsigned, quarter squares at SQLO/SQHI); clobbers a, c, d, e
mulq:
	ld	c, a
	add	a, b
	ld	l, a
	ld	a, #>SQLO
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
	ld	h, #>SQLO
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

; hl = F_D * (signed) a
smul:
	bit	7, a
	jr	nz, 1$
	ld	b, a
	ldh	a, (F_D)
	jp	mulq
1$:
	cpl
	inc	a
	ld	b, a
	ldh	a, (F_D)
	call	mulq
	xor	a, a
	sub	a, l
	ld	l, a
	ld	a, #0
	sbc	a, h
	ld	h, a
	ret

; hl >>= 4 (arithmetic)
sra4:
	sra	h
	rr	l
	sra	h
	rr	l
	sra	h
	rr	l
	sra	h
	rr	l
	ret

; a = texel byte at the floor point b = px hi, d = py hi (map at 0xD800: cell = (py>>11)*32 + (px>>11))
	.macro	SAMPLE	base
	ld	a, d
	rlca
	rlca
	ld	l, a
	and	a, #3
	or	a, #>MAPADDR
	ld	h, a
	ld	a, l
	and	a, #0xE0
	ld	l, a
	ld	a, b
	rrca
	rrca
	rrca
	and	a, #0x1F
	or	a, l
	ld	l, a
	ld	a, (hl)
	and	a, #7
	add	a, #>base
	ld	h, a
	ld	a, d
	swap	a
	and	a, #0xF0
	ld	l, a
	ld	a, b
	and	a, #0x0F
	or	a, l
	ld	l, a
	ld	a, (hl)
	.endm

	.macro	STEP xl, xh, yl, yh
	ldh	a, (xl)
	add	a, c
	ld	c, a
	ldh	a, (xh)
	adc	a, b
	ld	b, a
	ldh	a, (yl)
	add	a, e
	ld	e, a
	ldh	a, (yh)
	adc	a, d
	ld	d, a
	.endm

; one masked floor/ceiling pixel row (partial path). in: a = vis bits (2 = slot 0, 1 = slot 1), d = colour index
; ((c0 << 2) | c1), e = pixel row; OR into strip + row * 2
frow:
	; in: a = vis bits (2 = slot 0, 1 = slot 1), d = HRAM address of the (lo, hi) bytes, e = pixel row
	add	a, #<NIB
	ld	l, a
	ld	h, #>NIB
	ld	b, (hl)
	ld	a, e
	add	a, #<FPAT
	ld	l, a
	ld	h, #>FPAT
	ld	a, (hl)
	and	a, b
	ld	b, a
	ld	a, e
	add	a, a
	ld	l, a
	ldh	a, (F_STL)
	add	a, l
	ld	l, a
	ldh	a, (F_STH)
	ld	h, a
	ld	c, d
	ldh	a, (c)
	and	a, b
	or	a, (hl)
	ld	(hl+), a
	inc	c
	ldh	a, (c)
	and	a, b
	or	a, (hl)
	ld	(hl), a
	ret

; partial-path row writers. b rows (>= 1), d/e = masked lo/hi, c = <PFk / <PCk (row patterns in HRAM)
pup:
	ldh	a, (c)
	and	a, d
	or	a, (hl)
	ld	(hl+), a
	ldh	a, (c)
	and	a, e
	or	a, (hl)
	ld	(hl+), a
	inc	c
	dec	b
	jr	nz, pup
	ret

pst:
	ldh	a, (c)
	and	a, d
	ld	(hl+), a
	ldh	a, (c)
	and	a, e
	ld	(hl+), a
	inc	c
	dec	b
	jr	nz, pst
	ret

pdst:
	ldh	a, (c)
	and	a, d
	ld	(hl+), a
	ldh	a, (c)
	and	a, e
	ld	(hl-), a
	dec	hl
	dec	hl
	inc	c
	dec	b
	jr	nz, pdst
	ret

pdown:
	ldh	a, (c)
	and	a, d
	or	a, (hl)
	ld	(hl+), a
	ldh	a, (c)
	and	a, e
	or	a, (hl)
	ld	(hl-), a
	dec	hl
	dec	hl
	inc	c
	dec	b
	jr	nz, pdown
	ret

; one slot of a partial tile: k0 = max(fv - y0, 0); rows k0..FR-1. Slot 0 (ofv/ocv = slot 1's rows, split = 1)
; ORs only the rows where slot 1 shows wall (the walls zeroed just those rows) and stores the rest outright
; (slot 1 then ORs its nibble in), so the walls need not zero the slot-0 rows that only floors cover.
	.macro	PSPLIT	ofv, pw, fr
	ldh	(F_YS), a
	ld	a, #fr
	sub	a, c
	ld	b, a
	ld	a, c
	add	a, #<pw
	ld	c, a
	ldh	a, (F_YS)
	ld	d, a
	ldh	a, (ofv)
	sub	a, d
	jr	nc, .+3
	xor	a, a
	cp	a, b
	jr	c, .+3
	ld	a, b
	ld	d, a
	ld	a, b
	sub	a, d
	ldh	(F_NST), a
	ld	b, d
	.endm

	.macro	PSLOT	fv, cv, mask, fr, split, ofv, ocv, ?l1, ?l2, ?l3, ?l4, ?l5, ?l6
	ldh	a, (F_Y0)
	ld	b, a
	ldh	a, (fv)
	sub	a, b
	jr	nc, l1
	xor	a, a
l1:
	cp	a, #fr
	jp	nc, l2
	ld	c, a
	add	a, b
.if split
	push	af
.endif
	add	a, a
	ld	l, a
	ldh	a, (F_STL)
	add	a, l
	ld	l, a
	ldh	a, (F_STH)
	ld	h, a
.if split
	pop	af
	PSPLIT	ofv, PF0, fr
.else
	ld	a, #fr
	sub	a, c
	ld	b, a
	ld	a, c
	add	a, #<PF0
	ld	c, a
.endif
	ldh	a, (P_FL)
	and	a, #mask
	ld	d, a
	ldh	a, (P_FL + 1)
	and	a, #mask
	ld	e, a
.if split
	inc	b
	dec	b
	call	nz, pup
	ldh	a, (F_NST)
	or	a, a
	jr	z, l2
	ld	b, a
	call	pst
.else
	call	pup
.endif
l2:
	ldh	a, (F_Y0)
	ld	b, a
	ldh	a, (cv)
	sub	a, b
	jr	nc, l3
	xor	a, a
l3:
	cp	a, #fr
	jp	nc, l4
	ld	c, a
	add	a, b
.if split
	push	af
.endif
	cpl
	add	a, #VIEW_H
	add	a, a
	ld	l, a
	ldh	a, (F_STL)
	add	a, l
	ld	l, a
	ldh	a, (F_STH)
	ld	h, a
.if split
	pop	af
	PSPLIT	ocv, PC0, fr
.else
	ld	a, #fr
	sub	a, c
	ld	b, a
	ld	a, c
	add	a, #<PC0
	ld	c, a
.endif
	ldh	a, (P_CL)
	and	a, #mask
	ld	d, a
	ldh	a, (P_CL + 1)
	and	a, #mask
	ld	e, a
.if split
	inc	b
	dec	b
	call	nz, pdown
	ldh	a, (F_NST)
	or	a, a
	jr	z, l4
	ld	b, a
	call	pdst
.else
	call	pdown
.endif
l4:
	.endm

	.macro	FROWS	fr, ?f310, ?f320, ?f330, ?f340, ?f350, ?f351, ?f352, ?f380
	; row patterns: floor rows y0.., ceiling rows H-1-y0..; NOFOG when all are 0xFF
	ld	b, #0xFF
	ldh	a, (F_Y0)
	add	a, #<FPAT
	ld	l, a
	ld	h, #>FPAT
	ld	a, (hl+)
	ldh	(PF0), a
	and	a, b
	ld	b, a
	ld	a, (hl+)
	ldh	(PF1), a
	and	a, b
	ld	b, a
.if fr - 2
	ld	a, (hl+)
	ldh	(PF2), a
	and	a, b
	ld	b, a
.ifeq fr - 4
	ld	a, (hl)
	ldh	(PF3), a
	and	a, b
	ld	b, a
.endif
.ifeq fr - 8
	ld	a, (hl+)
	ldh	(PF3), a
	and	a, b
	ld	b, a
	ld	a, (hl+)
	ldh	(PF4), a
	and	a, b
	ld	b, a
	ld	a, (hl+)
	ldh	(PF5), a
	and	a, b
	ld	b, a
	ld	a, (hl+)
	ldh	(PF6), a
	and	a, b
	ld	b, a
	ld	a, (hl)
	ldh	(PF7), a
	and	a, b
	ld	b, a
.endif
.endif
	ldh	a, (F_Y0)
	cpl
	add	a, #VIEW_H + <FPAT
	ld	l, a
	ld	a, (hl-)
	ldh	(PC0), a
	and	a, b
	ld	b, a
	ld	a, (hl-)
	ldh	(PC1), a
	and	a, b
	ld	b, a
.if fr - 2
	ld	a, (hl-)
	ldh	(PC2), a
	and	a, b
	ld	b, a
.ifeq fr - 4
	ld	a, (hl)
	ldh	(PC3), a
	and	a, b
	ld	b, a
.endif
.ifeq fr - 8
	ld	a, (hl-)
	ldh	(PC3), a
	and	a, b
	ld	b, a
	ld	a, (hl-)
	ldh	(PC4), a
	and	a, b
	ld	b, a
	ld	a, (hl-)
	ldh	(PC5), a
	and	a, b
	ld	b, a
	ld	a, (hl-)
	ldh	(PC6), a
	and	a, b
	ld	b, a
	ld	a, (hl)
	ldh	(PC7), a
	and	a, b
	ld	b, a
.endif
.endif
	ld	a, b
	ldh	(NOFOG), a
	; steps: s = (d * u) >> 4, s2 = 2 s
	ld	a, (_f_ux)
	call	smul
	call	sra4
	ld	a, l
	ldh	(F_SXL), a
	ld	a, h
	ldh	(F_SXH), a
	add	hl, hl
	ld	a, l
	ldh	(F_S2XL), a
	ld	a, h
	ldh	(F_S2XH), a
	ld	a, (_f_uy)
	call	smul
	call	sra4
	ld	a, l
	ldh	(F_SYL), a
	ld	a, h
	ldh	(F_SYH), a
	add	hl, hl
	ld	a, l
	ldh	(F_S2YL), a
	ld	a, h
	ldh	(F_S2YH), a
	; ray-0 point: py into the stack, px into bc
	ld	a, (_f_ly)
	call	smul
	add	hl, hl
	ld	a, (_f_cy8)
	add	a, l
	ld	e, a
	ld	a, (_f_cy8 + 1)
	adc	a, h
	ld	d, a
	push	de
	ld	a, (_f_lx)
	call	smul
	add	hl, hl
	ld	a, (_f_cx8)
	add	a, l
	ld	c, a
	ld	a, (_f_cx8 + 1)
	adc	a, h
	ld	b, a
	pop	de
	ld	hl, #_w_buf
	ld	a, (hl+)
	ldh	(F_STL), a
	ld	a, (hl)
	ldh	(F_STH), a
	xor	a, a
	ldh	(F_T), a
f310:
	; any floor in this tile column on these rows?
	ldh	a, (F_T)
	add	a, #FMIN
	ld	l, a
	ld	h, #0xFF
	ld	a, (hl)
	ld	l, a
	ldh	a, (F_YL)
	cp	a, l
	jp	nc, f320
	STEP	F_S2XL, F_S2XH, F_S2YL, F_S2YH
	jp	f380
f320:
	; all visible on every row?
	ldh	a, (F_T)
	add	a, #CMAX
	ld	l, a
	ld	a, (hl)
	ld	l, a
	ldh	a, (F_Y0)
	cp	a, l
	jp	c, f350
	; ---- fast path ----
	SAMPLE	FTEX
	ldh	(F_B0), a
	STEP	F_SXL, F_SXH, F_SYL, F_SYH
	SAMPLE	FTEX1
	ld	l, a
	ldh	a, (F_B0)
	or	a, l
	ld	l, a
	STEP	F_SXL, F_SXH, F_SYL, F_SYH
	push	bc
	push	de
	; floor bytes d/e, ceiling bytes b/c
	ld	h, #>FLUT
	ld	d, (hl)
	inc	h
	ld	e, (hl)
	inc	h
	ld	b, (hl)
	inc	h
	ld	c, (hl)
	; floor rows from y0
	ldh	a, (F_Y0)
	add	a, a
	ld	l, a
	ldh	a, (F_STL)
	add	a, l
	ld	l, a
	ldh	a, (F_STH)
	ld	h, a
	ldh	a, (NOFOG)
	inc	a
	jp	nz, f330
	; no fog: plain stores
	.rept	fr
	ld	a, d
	ld	(hl+), a
	ld	a, e
	ld	(hl+), a
	.endm
	ldh	a, (F_Y0)
	cpl
	add	a, #VIEW_H + 1 - fr
	add	a, a
	ld	l, a
	ldh	a, (F_STL)
	add	a, l
	ld	l, a
	ldh	a, (F_STH)
	ld	h, a
	.rept	fr
	ld	a, b
	ld	(hl+), a
	ld	a, c
	ld	(hl+), a
	.endm
	jp	f340
f330:
	ldh	a, (PF0)
	and	a, d
	ld	(hl+), a
	ldh	a, (PF0)
	and	a, e
	ld	(hl+), a
	ldh	a, (PF1)
	and	a, d
	ld	(hl+), a
	ldh	a, (PF1)
	and	a, e
	ld	(hl+), a
.if fr - 2
	ldh	a, (PF2)
	and	a, d
	ld	(hl+), a
	ldh	a, (PF2)
	and	a, e
	ld	(hl+), a
	ldh	a, (PF3)
	and	a, d
	ld	(hl+), a
	ldh	a, (PF3)
	and	a, e
	ld	(hl+), a
.ifeq fr - 8
	ldh	a, (PF4)
	and	a, d
	ld	(hl+), a
	ldh	a, (PF4)
	and	a, e
	ld	(hl+), a
	ldh	a, (PF5)
	and	a, d
	ld	(hl+), a
	ldh	a, (PF5)
	and	a, e
	ld	(hl+), a
	ldh	a, (PF6)
	and	a, d
	ld	(hl+), a
	ldh	a, (PF6)
	and	a, e
	ld	(hl+), a
	ldh	a, (PF7)
	and	a, d
	ld	(hl+), a
	ldh	a, (PF7)
	and	a, e
	ld	(hl+), a
.endif
.endif
	; ceiling rows H-FR-y0 .. H-1-y0 (top to bottom: PC(FR-1) .. PC0)
	ldh	a, (F_Y0)
	cpl
	add	a, #VIEW_H + 1 - fr
	add	a, a
	ld	l, a
	ldh	a, (F_STL)
	add	a, l
	ld	l, a
	ldh	a, (F_STH)
	ld	h, a
.ifeq fr - 8
	ldh	a, (PC7)
	and	a, b
	ld	(hl+), a
	ldh	a, (PC7)
	and	a, c
	ld	(hl+), a
	ldh	a, (PC6)
	and	a, b
	ld	(hl+), a
	ldh	a, (PC6)
	and	a, c
	ld	(hl+), a
	ldh	a, (PC5)
	and	a, b
	ld	(hl+), a
	ldh	a, (PC5)
	and	a, c
	ld	(hl+), a
	ldh	a, (PC4)
	and	a, b
	ld	(hl+), a
	ldh	a, (PC4)
	and	a, c
	ld	(hl+), a
.endif
.if fr - 2
	ldh	a, (PC3)
	and	a, b
	ld	(hl+), a
	ldh	a, (PC3)
	and	a, c
	ld	(hl+), a
	ldh	a, (PC2)
	and	a, b
	ld	(hl+), a
	ldh	a, (PC2)
	and	a, c
	ld	(hl+), a
.endif
	ldh	a, (PC1)
	and	a, b
	ld	(hl+), a
	ldh	a, (PC1)
	and	a, c
	ld	(hl+), a
	ldh	a, (PC0)
	and	a, b
	ld	(hl+), a
	ldh	a, (PC0)
	and	a, c
	ld	(hl), a
f340:
	pop	de
	pop	bc
	jp	f380
f350:
	; ---- partial path: per-ray visibility, per-row masks ----
	ldh	a, (F_T)
	add	a, a
	add	a, a
	ld	l, a
	ld	h, #>REC
	ld	a, (hl+)
	ldh	(F_FV0), a
	ld	a, (hl+)
	ldh	(F_FV1), a
	ld	a, (hl+)
	ldh	(F_CV0), a
	ld	a, (hl)
	ldh	(F_CV1), a
	xor	a, a
	ldh	(F_B0), a
	ldh	(F_B1), a
	ldh	a, (F_FV0)
	ld	l, a
	ldh	a, (F_YL)
	cp	a, l
	jp	c, f351
	SAMPLE	FTEX
	ldh	(F_B0), a
f351:
	STEP	F_SXL, F_SXH, F_SYL, F_SYH
	ldh	a, (F_FV1)
	ld	l, a
	ldh	a, (F_YL)
	cp	a, l
	jp	c, f352
	SAMPLE	FTEX1
	ldh	(F_B1), a
f352:
	STEP	F_SXL, F_SXH, F_SYL, F_SYH
	push	bc
	push	de
	ldh	a, (F_B0)
	ld	l, a
	ldh	a, (F_B1)
	or	a, l
	ld	l, a
	ld	h, #>FLUT
	ld	c, #<P_FL
	ld	a, (hl)
	ldh	(c), a
	inc	c
	inc	h
	ld	a, (hl)
	ldh	(c), a
	inc	c
	inc	h
	ld	a, (hl)
	ldh	(c), a
	inc	c
	inc	h
	ld	a, (hl)
	ldh	(c), a
	; per slot: floor rows from max(fv, y0), ceiling rows (mirrored) from max(cv, y0); OR into zeroed rows
	PSLOT	F_FV0, F_CV0, 0xF0, fr, 1, F_FV1, F_CV1
	PSLOT	F_FV1, F_CV1, 0x0F, fr, 0, F_FV1, F_CV1
	pop	de
	pop	bc
f380:
	; next tile column (strips are 128 bytes, page-aligned buffers)
	ldh	a, (F_STL)
	add	a, #<STRIP
	ldh	(F_STL), a
	ldh	a, (F_STH)
	adc	a, #>STRIP
	ldh	(F_STH), a
	ldh	a, (F_T)
	inc	a
	ldh	(F_T), a
	cp	a, #TW
	jp	nz, f310
	jp	390$
	.endm

_floors_asm::
	ld	a, #3
	ldh	(__current_bank), a
	ld	(0x2000), a
	; camera-angle constants from LKT (bank 3)
	ld	a, (_cam_a)
	ld	l, a
	ld	a, (_cam_a + 1)
	and	a, #3
	add	a, #>LKT
	ld	h, a
	ld	a, (hl)
	ld	(_f_lx), a
	ld	a, h
	add	a, #4
	ld	h, a
	ld	a, (hl)
	ld	(_f_ly), a
	ld	a, h
	add	a, #4
	ld	h, a
	ld	a, (hl)
	ld	(_f_ux), a
	ld	a, h
	add	a, #4
	ld	h, a
	ld	a, (hl)
	ld	(_f_uy), a
	; per tile: REC {fv0, fv1, cv0, cv1}, FMIN = min fv, CMAX = max cv; f_minfv
	ld	a, #VIEW_H
	ld	(_f_minfv), a
	ld	hl, #_ray_fv
	ld	de, #REC
	ld	c, #FMIN
1$:
	ld	a, (hl+)
	ld	(de), a
	inc	e
	ld	b, a
	ld	a, (hl+)
	ld	(de), a
	inc	e
	inc	e
	inc	e
	cp	a, b
	jr	c, 2$
	ld	a, b
2$:
	ldh	(c), a
	ld	b, a
	ld	a, (_f_minfv)
	cp	a, b
	jr	c, 3$
	ld	a, b
	ld	(_f_minfv), a
3$:
	inc	c
	ld	a, c
	cp	a, #FMIN + TW
	jr	nz, 1$
	ld	hl, #_ray_cv
	ld	de, #REC + 2
	ld	c, #CMAX
4$:
	ld	a, (hl+)
	ld	(de), a
	inc	e
	ld	b, a
	ld	a, (hl+)
	ld	(de), a
	inc	e
	inc	e
	inc	e
	cp	a, b
	jr	nc, 5$
	ld	a, b
5$:
	ldh	(c), a
	inc	c
	ld	a, c
	cp	a, #CMAX + TW
	jr	nz, 4$
	xor	a, a
	ldh	(F_J), a
300$:
	ldh	a, (F_J)
	ld	c, a
	add	a, #<FY0
	ld	l, a
	ld	h, #>FY0
	ld	a, (hl)
	ldh	(F_Y0), a
	ld	b, a
	ld	a, c
	add	a, #<FN
	ld	l, a
	ld	a, (hl)
	ldh	(F_N), a
	add	a, b
	dec	a
	ldh	(F_YL), a
	ld	hl, #_f_minfv
	cp	a, (hl)
	jp	c, 390$
	ld	a, c
	add	a, #<FD
	ld	l, a
	ld	h, #>FD
	ld	a, (hl)
	or	a, a
	jp	z, 390$
	ldh	(F_D), a
	ldh	a, (F_N)
	cp	a, #4
	jp	z, 3004$
	cp	a, #8
	jp	z, 3008$
	FROWS	2
3004$:
	FROWS	4
3008$:
	FROWS	8
390$:
	ldh	a, (F_J)
	inc	a
	ldh	(F_J), a
	cp	a, #NFROW
	jp	nz, 300$
	ld	a, #1
	ldh	(__current_bank), a
	ld	(0x2000), a
	ret

.if OBJ_ON
; oam_dma(page): the HRAM OAM DMA routine GBDK installs at 0xFF84 (A = source page)
_oam_dma::
	jp	0xFF84

; ---------------------------------------------------------------------------------------------------------------
; bb_col: one billboard column (bbe_nr 8x16 sprites from bbc_src in bank bbc_bank) into the staging area and OAM:
; rows clipped to the view (bbe_y8 = top + 64), ANDed with the column mask bbc_mask (4 px occlusion)
BC_Y8 = 0xFFA4
BC_N = 0xFFA5
BC_SL = 0xFFA6
BC_SH = 0xFFA7
BC_R0 = 0xFFA8
BC_R1 = 0xFFA9
STAGE_MAX = STAGEMAX
_bb_col::
	ld	a, (_bbc_bank)
	ldh	(__current_bank), a
	ld	(0x2000), a
	ld	a, (_bbe_y8)
	ldh	(BC_Y8), a
	ld	a, (_bbe_nr)
	ldh	(BC_N), a
	ld	a, (_bbc_src)
	ldh	(BC_SL), a
	ld	a, (_bbc_src + 1)
	ldh	(BC_SH), a
10$:
	ldh	a, (BC_Y8)
	cp	a, #49
	jp	c, 30$
	cp	a, #64 + VSH
	jp	nc, 40$
	ld	a, (_obj_stage_n)
	cp	a, #STAGE_MAX
	jp	nc, 40$
	ld	a, (_obj_on)
	cp	a, #40
	jp	nc, 40$
	; r0 = max(64 - y8, 0), r1 = min(128 - y8, 16)
	ldh	a, (BC_Y8)
	cp	a, #64
	jr	nc, 11$
	ld	c, a
	ld	a, #64
	sub	a, c
	jr	12$
11$:
	xor	a, a
12$:
	ldh	(BC_R0), a
	ldh	a, (BC_Y8)
	cp	a, #64 + VSH - 15
	jr	c, 13$
	ld	c, a
	ld	a, #64 + VSH
	sub	a, c
	jr	14$
13$:
	ld	a, #16
14$:
	ldh	(BC_R1), a
	; hl = STAGE + stage_n * 32
	ld	a, (_obj_stage_n)
	ld	c, a
	and	a, #7
	rrca
	rrca
	rrca
	ld	l, a
	ld	a, c
	srl	a
	srl	a
	srl	a
	add	a, #>STAGEADDR
	ld	h, a
	; de = src + r0 * 2
	ldh	a, (BC_R0)
	add	a, a
	ld	c, a
	ldh	a, (BC_SL)
	add	a, c
	ld	e, a
	ldh	a, (BC_SH)
	adc	a, #0
	ld	d, a
	ldh	a, (BC_R0)
	or	a, a
	jr	z, 16$
	ld	b, a
	xor	a, a
15$:
	ld	(hl+), a
	ld	(hl+), a
	dec	b
	jr	nz, 15$
16$:
	ldh	a, (BC_R0)
	ld	b, a
	ldh	a, (BC_R1)
	sub	a, b
	jr	z, 18$
	ld	b, a
	ld	a, (_bbc_mask)
	ld	c, a
17$:
	ld	a, (de)
	inc	de
	and	a, c
	ld	(hl+), a
	ld	a, (de)
	inc	de
	and	a, c
	ld	(hl+), a
	dec	b
	jr	nz, 17$
18$:
	ldh	a, (BC_R1)
	ld	c, a
	ld	a, #16
	sub	a, c
	jr	z, 20$
	ld	b, a
	xor	a, a
19$:
	ld	(hl+), a
	ld	(hl+), a
	dec	b
	jr	nz, 19$
20$:
	; OAM: y = y8 - 8 (view at y 40), x, tile 96 + 2 n, attr
	ld	hl, #_obj_oam
	ld	a, (hl+)
	ld	h, (hl)
	ld	l, a
	ldh	a, (BC_Y8)
	sub	a, #48 - VY0
	ld	(hl+), a
	ld	a, (_bbe_ox)
	ld	(hl+), a
	ld	a, (_obj_stage_n)
	add	a, a
	add	a, #OBJT0
	ld	(hl+), a
	ld	a, (_bbe_attr)
	ld	(hl+), a
	ld	a, l
	ld	(_obj_oam), a
	ld	a, h
	ld	(_obj_oam + 1), a
	ld	hl, #_obj_stage_n
	inc	(hl)
	ld	hl, #_obj_on
	inc	(hl)
30$:
	ldh	a, (BC_SL)
	add	a, #32
	ldh	(BC_SL), a
	ldh	a, (BC_SH)
	adc	a, #0
	ldh	(BC_SH), a
	ldh	a, (BC_Y8)
	add	a, #16
	ldh	(BC_Y8), a
	ldh	a, (BC_N)
	dec	a
	ldh	(BC_N), a
	jp	nz, 10$
40$:
	ld	a, #1
	ldh	(__current_bank), a
	ld	(0x2000), a
	ret

; vis_fill: vpad[j0 + 2 + i] = vf_d < ray_d[j0 + i] ? 0x0F : 0, i < vf_n (the billboard's per-ray visibility)
_vis_fill_asm::
	ld	a, (_vf_j0)
	ld	c, a
	ld	b, #0
	ld	hl, #_vpad + 2
	add	hl, bc
	ld	e, l
	ld	d, h
	ld	hl, #_ray_d
	add	hl, bc
	add	hl, bc
	ld	a, (_vf_n)
	ld	b, a
	ld	a, (_vf_d)
	ld	c, a
1$:
	ld	a, c
	sub	a, (hl)
	inc	hl
	ld	a, (_vf_d + 1)
	sbc	a, (hl)
	inc	hl
	sbc	a, a
	and	a, #0x0F
	ld	(de), a
	inc	de
	dec	b
	jr	nz, 1$
	ret
.endif
