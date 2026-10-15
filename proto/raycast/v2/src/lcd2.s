; Fullscreen row doubling for the v2 raycaster (assembled after VIEW_H from build2.sh).
;
; The framebuffer's VIEW_H rows sit in BG map rows 0..VIEW_H/8-1 and each is shown on two LCD lines: on every odd
; line 1, 3, .., 2*VIEW_H-1 an LY=LYC interrupt steps SCY back by one, so line 2r and 2r+1 both fetch BG line r.
; The write lands in mode 2 of that line (the interrupt is requested when LY changes, at the start of OAM scan;
; SCY is first read for the line's tile fetch in mode 3). After the last pair SCY stays at -VIEW_H, so lines
; 2*VIEW_H.. show map rows VIEW_H/8.. (the HUD) with no extra write; vbl_isr puts SCY back to 0.
; The last interrupt also swaps BG palette 0 to the HUD's colours in that line's HBlank (palette RAM is
; writable outside mode 3); vbl_isr restores the view's palette 0.
	.module lcd2
	.globl _lyc_isr, _hud_pal
.if TT
	.globl _tt_scy_bad, _tt_pal_bad, _tt_dma_t, _tt_end_t, _tt_scy_mode, _tt_scy_ly
.endif

LASTL = 2 * VIEW_H - 1

	.area _CODE	; linked first: ROM0 (the ISR can fire while a data bank is mapped at 0x4000)
_lyc_isr::
	push	af
	ldh	a, (0x42)	; SCY
	dec	a
.if TT
	.rept	TT_DELAY	; trick test: delay the write to find the margin
	nop
	.endm
.endif
	ldh	(0x42), a
.if TT
	; trick test: the write must have landed in mode 2 (read after it, so a write in the last 3 cycles of
	; mode 2 counts as a miss)
	ldh	a, (0x41)
	and	a, #3
	cp	a, #2
	jr	z, 7$
	ld	(_tt_scy_mode), a
	ldh	a, (0x44)
	ld	(_tt_scy_ly), a
	cp	a, #144		; the LY=144 entry (VBlank work) also passes here: not a doubled line
	jr	nc, 7$
	ld	a, (_tt_scy_bad)
	inc	a
	jr	z, 7$
	ld	(_tt_scy_bad), a
7$:
	ldh	a, (0x42)
.endif
	ldh	a, (0x45)	; LYC += 2
	add	a, #2
	ldh	(0x45), a
	cp	a, #LASTL + 2
	jr	nc, 1$
	pop	af
	reti
1$:
	; two rare cases share the exit: LYC was LASTL (the HUD starts next line) or 144 (VBlank: the copy + flip;
	; the VBlank interrupt itself stays off, so GBDK's dispatcher never costs VBlank time)
	cp	a, #144 + 2
	jp	z, 9$
	push	hl
	ld	a, #144
	ldh	(0x45), a
2$:
	ldh	a, (0x41)	; wait for this line's HBlank (mode 0)
	and	a, #3
	jr	nz, 2$
	ld	a, #0x80
	ldh	(0x68), a	; BCPS: palette 0, colour 0, auto-increment
	ld	hl, #_hud_pal
.if TT
	.rept	TT_PDELAY
	nop
	.endm
.endif
	.rept	8
	ld	a, (hl+)
	ldh	(0x69), a
	.endm
.if TT
	ldh	a, (0x41)
	and	a, #3
	cp	a, #3
	jr	nz, 8$
	ld	a, (_tt_pal_bad)
	inc	a
	jr	z, 8$
	ld	(_tt_pal_bad), a
8$:
.endif
	pop	hl
	pop	af
	reti
9$:
.if TT
	xor	a, a
	ldh	(0x05), a	; trick test: TIMA = 0 at line 144 (16-cycle ticks)
.endif
	ld	a, #1
	ldh	(0x45), a	; next frame starts again at line 1
	push	bc
	push	de
	push	hl
	call	vbl_fs
	pop	hl
	pop	de
	pop	bc
	pop	af
	reti

; ---------------------------------------------------------------------------------------------------------------
; vbl_fs: the VBlank half of the fullscreen double buffer (entered from lyc_isr at line 144).
; A finished view (pend) sits in WRAM bank 2 + pend_buf as one linear image of VRAM: its 160 view tiles at 0xD000
; then its billboard tiles (staging, OBJ tiles 160..) at 0xDA00, q_len = 160 + 2 n blocks of 16 bytes. Each VBlank
; GDMAs up to VBUDGET blocks (16 double-speed cycles each) of it to 0x8000 + the same offset in the hidden VRAM bank;
; once all are over and FLIPB blocks' worth of time is left, the flip: BG map (LCDC bit 3), OAM (DMA from 0xDE00)
; and the OBJ palettes the view uses (0-2 mobs, 7 crown/sparks; 6, the weapon, is fixed).
	.globl _vbl, _pend, _pend_buf, _front, _pal_dirty, _pal_cur, _hud_dirty, _hud_t
	.globl _log_flip, _pend_view, _nflips, _vbl_end_max, _copy_lines, _copy_calls
	.globl _q_len, _q_off
.if OBJ_ON
	.globl _built, _objpal
.endif
VBUDGET = 112
FLIPB = 22
HUDMAP = (TH + 1) * 32 + 18	; the two fps digits

	.area _DATA
vf_l0:	.ds 1
vf_bud:	.ds 1
_q_len:	.ds 1
_q_off:	.ds 1
	.area _CODE

; b blocks of GDMA time, for emulators that make GDMA free (PyBoy); NO_DMA_PAD builds skip it
vf_pad:
.if DMA_PAD
	push	af		; 16 cycles a block, as a double-speed GDMA block
	pop	af
	nop
	nop
	nop
	nop
	nop
	dec	b
	jr	nz, vf_pad
.endif
	ret

vbl_fs:
	xor	a, a
	ldh	(0x42), a		; SCY = 0 before line 0
	; BG palette 0 back to the view's top row (all 8 when a crossfade changed them)
	ld	a, #0x80
	ldh	(0x68), a
	ld	hl, #_pal_cur
	ld	c, #0x69
	ld	a, (_pal_dirty)
	or	a, a
	jr	nz, 2$
	.rept	8
	ld	a, (hl+)
	ldh	(c), a
	.endm
	jr	3$
2$:
	ld	b, #64
21$:
	ld	a, (hl+)
	ldh	(c), a
	dec	b
	jr	nz, 21$
	xor	a, a
	ld	(_pal_dirty), a
3$:
	; the HUD's fps digits (VBK is 0 outside this routine)
	ld	a, (_hud_dirty)
	or	a, a
	jr	z, 38$
	xor	a, a
	ld	(_hud_dirty), a
	ld	a, (_hud_t)
	ld	(0x9800 + HUDMAP), a
	ld	(0x9C00 + HUDMAP), a
	ld	a, (_hud_t + 1)
	ld	(0x9800 + HUDMAP + 1), a
	ld	(0x9C00 + HUDMAP + 1), a
38$:
	ld	hl, #_vbl
	inc	(hl)
	jr	nz, 4$
	inc	hl
	inc	(hl)
4$:
	ld	a, (_pend)
	or	a, a
	jp	z, 80$
	ldh	a, (0x44)
	ld	(vf_l0), a
	ldh	a, (0x70)
	push	af
	ld	a, (_pend_buf)
	add	a, #2
	ldh	(0x70), a		; SVBK: the view's WRAM bank
	ld	a, (_front)
	xor	a, #1
	ldh	(0x4F), a		; VBK: the hidden VRAM bank
	; c = min(q_len - q_off, VBUDGET); one GDMA of c blocks from 0xD000 + 16 q_off to 0x8000 + 16 q_off
	ld	a, (_q_off)
	ld	b, a
	ld	a, (_q_len)
	sub	a, b
	jr	z, 30$
	cp	a, #VBUDGET
	jr	c, 11$
	ld	a, #VBUDGET
11$:
	ld	c, a
	ld	a, #VBUDGET
	sub	a, c
	ld	(vf_bud), a
	ld	a, b
	add	a, c
	ld	(_q_off), a
	ld	l, b
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	a, h
	add	a, #0xD0
	ldh	(0x51), a
	ld	a, l
	ldh	(0x52), a
	ldh	(0x54), a
	ld	a, h
	add	a, #0x80
	ldh	(0x53), a
	ld	b, c
	ld	a, c
	dec	a
	ldh	(0x55), a		; GDMA (the CPU is halted while it runs)
.if TT
	ldh	a, (0x05)
	ld	hl, #_tt_dma_t
	cp	a, (hl)
	jr	c, 12$
	ld	(hl), a
12$:
.endif
	call	vf_pad
	jr	31$
30$:
	ld	a, #VBUDGET
	ld	(vf_bud), a
31$:
	; queue empty: flip when there is time left for it
	ld	a, (_q_off)
	ld	b, a
	ld	a, (_q_len)
	cp	a, b
	jp	nz, 70$
	ld	a, (vf_bud)
	cp	a, #FLIPB
	jp	c, 70$
	ld	a, (_front)
	xor	a, #1
	ld	(_front), a
	ldh	a, (0x40)
	xor	a, #0x08
	ldh	(0x40), a
.if OBJ_ON
	ld	a, (_pend_buf)
	ld	hl, #_built
	add	a, l
	ld	l, a
	jr	nc, 41$
	inc	h
41$:
	ld	a, (hl)
	or	a, a
	jr	z, 50$
	ld	a, #0xDE
	call	0xFF84			; OAM DMA from this view's buffer (GBDK's HRAM routine)
	; OBJ palettes 0-2 (mobs) and 7 (crown, sparks) of objpal[pend_buf]
	ld	hl, #_objpal
	ld	a, (_pend_buf)
	or	a, a
	jr	z, 42$
	ld	de, #64
	add	hl, de
42$:
	ld	a, #0x80
	ldh	(0x6A), a
	ld	c, #0x6B
	.rept	24
	ld	a, (hl+)
	ldh	(c), a
	.endm
	ld	de, #32
	add	hl, de
	ld	a, #0x80 + 56
	ldh	(0x6A), a
	.rept	8
	ld	a, (hl+)
	ldh	(c), a
	.endm
50$:
.endif
	; log_flip[pend_view & 63] = vbl ; nflips++ ; pend = 0
	ld	a, (_pend_view)
	and	a, #63
	add	a, a
	ld	e, a
	ld	d, #0
	ld	hl, #_log_flip
	add	hl, de
	ld	a, (_vbl)
	ld	(hl+), a
	ld	a, (_vbl + 1)
	ld	(hl), a
	ld	hl, #_nflips
	inc	(hl)
	jr	nz, 51$
	inc	hl
	inc	(hl)
51$:
	xor	a, a
	ld	(_pend), a
70$:
	pop	af
	ldh	(0x70), a
	xor	a, a
	ldh	(0x4F), a
	; stats: end line (154+ = spilled past VBlank), lines used
	ldh	a, (0x44)
	cp	a, #144
	jr	nc, 71$
	ld	b, a
	ldh	a, (0x41)
	and	a, #3
	cp	a, #1
	ld	a, #153		; LY reads 0 for most of line 153 (still mode 1)
	jr	z, 71$
	ld	a, b
	add	a, #154
71$:
	ld	b, a
	ld	a, (_vbl_end_max)
	cp	a, b
	jr	nc, 72$
	ld	a, b
	ld	(_vbl_end_max), a
72$:
	ld	a, (vf_l0)
	ld	c, a
	ld	a, b
	sub	a, c
	ld	hl, #_copy_lines
	add	a, (hl)
	ld	(hl+), a
	jr	nc, 73$
	inc	(hl)
73$:
	ld	hl, #_copy_calls
	inc	(hl)
	jr	nz, 80$
	inc	hl
	inc	(hl)
80$:
.if TT
	ldh	a, (0x05)
	ld	hl, #_tt_end_t
	cp	a, (hl)
	jr	c, 81$
	ld	(hl), a
81$:
.endif
	ret
