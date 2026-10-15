; EGRESS.EXE 3D renderer fill (SM83): t3d_walls_asm and t3d_floors_asm, from proto/raycast/v2/src/fill2.s (the prototype
; verified byte for byte against the host build of the same C). Changes for the game: the layout constants come from
; the generated gen/t3d_layout.inc; the map is at T3D_MAP (WRAM banks 2/3, see t3d.h); the table banks are the
; autobanked blobs ___bank_t3d_bank_w0 / _fl / _w1 (bank numbers from the linker); the routines leave their data
; bank mapped (t3d_render() in t3d_core.c restores the caller's bank); the billboard (OBJ) path is not included.
	.include "t3d_layout.inc"
	.globl ___bank_t3d_bank_w0, ___bank_t3d_bank_w1, ___bank_t3d_bank_fl
; Raycaster v2 fill (SM83): t3d_walls_asm and t3d_floors_asm. Same output as walls_all / floors_all in core2.c (the host
; build of that C is diffed against the cartridge). Assembled after the generated cfg (VOFF, VEND, STRIP, NRAYS,
; VIEW_H, TW) and layout.inc (bank blob addresses: walls data in ROM bank 2, floor data in ROM bank 3).
;
; Write scheme (equivalent to the C's masked writes): slot 0 (left 4 px of a tile byte) stores its wall rows with
; the right nibble 0 and zeroes all its other rows; slot 1 and the floor/ceiling pass then only OR into nibbles that
; are known to be 0.

	.module fill2
	.globl _t3d_ray_h, _t3d_ray_t, _t3d_ray_u, _t3d_ray_fv, _t3d_ray_cv
	.globl _t3d_walls_asm, _t3d_floors_asm, _t3d_wbuf
	.globl _t3d_f_lx, _t3d_f_ly, _t3d_f_ux, _t3d_f_uy, _t3d_f_cx8, _t3d_f_cy8, _t3d_f_minfv
	.globl __current_bank
	.globl _t3d_ca
.if OBJ_ON
	.globl _vis_fill_asm, _vf_j0, _vf_n, _vf_d, _vpad, _t3d_ray_d
	.globl _bb_col, _bbe_y8, _bbe_nr, _bbe_ox, _bbe_attr, _obj_oam, _obj_on, _obj_stage_n
.endif
	.globl _bbc_src, _bbc_mask, _bbc_bank
.if OBJ_ON
	.globl _oam_dma
.endif

	.area _DATA
_t3d_wbuf:	.ds 2
_t3d_f_lx:	.ds 1
_t3d_f_ly:	.ds 1
_t3d_f_ux:	.ds 1
_t3d_f_uy:	.ds 1
_t3d_f_cx8:	.ds 2
_t3d_f_cy8:	.ds 2
_t3d_f_minfv:	.ds 1
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
CMAX = 0xD8		; HRAM 0xFFD8: per tile column, first row with all floor + ceiling visible
F_B1 = 0xFFE8
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
; t3d_walls_asm: every ray column's textured, fogged wall run list (ROM bank 2 mapped while it runs).
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

_t3d_walls_asm::
	ld	a, #<___bank_t3d_bank_w0
	ldh	(__current_bank), a
	ld	(0x2000), a
	ld	hl, #_t3d_wbuf
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
	ld	hl, #_t3d_ray_h
	add	hl, bc
	ld	a, (hl)
	ldh	(W_H), a
	cp	a, #MIPH
	ld	a, #<___bank_t3d_bank_w0
	jr	nc, 1011$
	ld	a, #<___bank_t3d_bank_w1
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
	ld	hl, #_t3d_ray_t
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
	add	a, #<_t3d_ray_u
	ld	e, a
	ld	a, #0
	adc	a, #>_t3d_ray_u
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
	; t3d_ray_cv[i] = vt <= VOFF ? H : H + VOFF - vt
	ld	a, d
	cp	a, #VOFF + 1
	ld	a, #VIEW_H
	jr	c, 103$
	ld	a, #VIEW_H + VOFF
	sub	a, d
103$:
	ld	hl, #_t3d_ray_cv
	add	hl, bc
	ld	(hl), a
	; bot = vt + h ; t3d_ray_fv[i] = bot >= VEND ? H : bot - VOFF
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
	ld	hl, #_t3d_ray_fv
	add	hl, bc
	ld	(hl), a
	; slot 0 zeroes its rows above and below the wall
	ldh	a, (W_SLOT)
	or	a, a
	jr	nz, 110$
	ld	a, d
	cp	a, #VOFF + 1
	jr	c, 105$
	sub	a, #VOFF
	ld	b, a
	ldh	a, (W_STL)
	ld	l, a
	ldh	a, (W_STH)
	ld	h, a
	call	ws_zero
105$:
	ld	a, e
	cp	a, #VEND
	jr	nc, 110$
	ld	c, a
	ld	a, #VEND
	sub	a, c
	ld	b, a
	ld	a, c
	sub	a, #VOFF
	add	a, a
	ld	c, a
	ldh	a, (W_STL)
	add	a, c
	ld	l, a
	ldh	a, (W_STH)
	adc	a, #0		; strips are 144 bytes (96x72): they cross pages
	ld	h, a
	call	ws_zero
110$:
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
	adc	a, #0		; strips are 144 bytes (96x72): they cross pages
	ld	h, a
	ldh	a, (W_SLOT)
	or	a, a
	jp	nz, wl1
wl0:
	RUNLOOP	0, ws_set, wd_set, wp_set
	jp	wdone
wl1:
	RUNLOOP	1, ws_or, wd_or, wp_or
wdone:
	ldh	a, (W_I)
	inc	a
	ldh	(W_I), a
	cp	a, #NRAYS
	jp	nz, wcol
	ret

; ---------------------------------------------------------------------------------------------------------------
; t3d_floors_asm: floor + ceiling, row-major (ROM bank 3 mapped: quarter squares, sets, row tables).
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

; a = texel byte at the floor point b = px hi, d = py hi (map at T3D_MAP: cell = (py>>11)*32 + (px>>11))
	.macro	SAMPLE	base
	ld	a, d
	rlca
	rlca
	ld	l, a
	and	a, #3
	or	a, #MAPHI
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
	adc	a, #0		; strips are 144 bytes (96x72): they cross pages
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

; one slot of a partial tile: k0 = max(fv - y0, 0); rows k0..FR-1
	.macro	PSLOT	fv, cv, mask, fr, ?l1, ?l2, ?l3, ?l4
	ldh	a, (F_Y0)
	ld	b, a
	ldh	a, (fv)
	sub	a, b
	jr	nc, l1
	xor	a, a
l1:
	cp	a, #fr
	jr	nc, l2
	ld	c, a
	add	a, b
	add	a, a
	ld	l, a
	ldh	a, (F_STL)
	add	a, l
	ld	l, a
	ldh	a, (F_STH)
	adc	a, #0		; strips are 144 bytes (96x72): they cross pages
	ld	h, a
	ld	a, #fr
	sub	a, c
	ld	b, a
	ld	a, c
	add	a, #<PF0
	ld	c, a
	ldh	a, (P_FL)
	and	a, #mask
	ld	d, a
	ldh	a, (P_FL + 1)
	and	a, #mask
	ld	e, a
	call	pup
l2:
	ldh	a, (F_Y0)
	ld	b, a
	ldh	a, (cv)
	sub	a, b
	jr	nc, l3
	xor	a, a
l3:
	cp	a, #fr
	jr	nc, l4
	ld	c, a
	add	a, b
	cpl
	add	a, #VIEW_H
	add	a, a
	ld	l, a
	ldh	a, (F_STL)
	add	a, l
	ld	l, a
	ldh	a, (F_STH)
	adc	a, #0		; strips are 144 bytes (96x72): they cross pages
	ld	h, a
	ld	a, #fr
	sub	a, c
	ld	b, a
	ld	a, c
	add	a, #<PC0
	ld	c, a
	ldh	a, (P_CL)
	and	a, #mask
	ld	d, a
	ldh	a, (P_CL + 1)
	and	a, #mask
	ld	e, a
	call	pdown
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
	ld	a, (_t3d_f_ux)
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
	ld	a, (_t3d_f_uy)
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
	ld	a, (_t3d_f_ly)
	call	smul
	add	hl, hl
	ld	a, (_t3d_f_cy8)
	add	a, l
	ld	e, a
	ld	a, (_t3d_f_cy8 + 1)
	adc	a, h
	ld	d, a
	push	de
	ld	a, (_t3d_f_lx)
	call	smul
	add	hl, hl
	ld	a, (_t3d_f_cx8)
	add	a, l
	ld	c, a
	ld	a, (_t3d_f_cx8 + 1)
	adc	a, h
	ld	b, a
	pop	de
	ld	hl, #_t3d_wbuf
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
	adc	a, #0		; strips are 144 bytes (96x72): they cross pages
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
	adc	a, #0		; strips are 144 bytes (96x72): they cross pages
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
	adc	a, #0		; strips are 144 bytes (96x72): they cross pages
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
	PSLOT	F_FV0, F_CV0, 0xF0, fr
	PSLOT	F_FV1, F_CV1, 0x0F, fr
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

_t3d_floors_asm::
	ld	a, #<___bank_t3d_bank_fl
	ldh	(__current_bank), a
	ld	(0x2000), a
	; camera-angle constants from LKT (bank 3)
	ld	a, (_t3d_ca)
	ld	l, a
	ld	a, (_t3d_ca + 1)
	and	a, #3
	add	a, #>LKT
	ld	h, a
	ld	a, (hl)
	ld	(_t3d_f_lx), a
	ld	a, h
	add	a, #4
	ld	h, a
	ld	a, (hl)
	ld	(_t3d_f_ly), a
	ld	a, h
	add	a, #4
	ld	h, a
	ld	a, (hl)
	ld	(_t3d_f_ux), a
	ld	a, h
	add	a, #4
	ld	h, a
	ld	a, (hl)
	ld	(_t3d_f_uy), a
	; per tile: REC {fv0, fv1, cv0, cv1}, FMIN = min fv, CMAX = max cv; t3d_f_minfv
	ld	a, #VIEW_H
	ld	(_t3d_f_minfv), a
	ld	hl, #_t3d_ray_fv
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
	ld	a, (_t3d_f_minfv)
	cp	a, b
	jr	c, 3$
	ld	a, b
	ld	(_t3d_f_minfv), a
3$:
	inc	c
	ld	a, c
	cp	a, #FMIN + TW
	jr	nz, 1$
	ld	hl, #_t3d_ray_cv
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
	ld	hl, #_t3d_f_minfv
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
	ret

