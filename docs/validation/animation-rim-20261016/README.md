# Continuing animation and peripheral construction

The previous release could defer a new full heir pose every time the draw pass ran late. A real holdout/movement reproduction left a requested pose unserved for 38 active samples while its logical animation advanced. Keeping all body layers visible did not prove that their animation progressed.

Deferred poses now get an early opportunity before speculative terrain work. Gravity and action replacement have priority, and intentionally invisible damage-blink/fall frames do not spend time composing hidden bodies. A pending complete upload cannot be overwritten by a second composition. The compressed stream stops after its final selected gene, including selected empty deltas.

Office combat reuses inactive world-memory banks 2 through 5 for 120 complete composed poses in 60 two-way sets. Cache leaves run without stack access while SVBK is switched, restore SVBK before returning, and run within a critical section. The pixel buffer and cache enable flag stay in fixed WRAM. A new expression disables reuse; floor sprite initialization clears the keys. Generated worlds keep exclusive ownership of those banks and bypass pose caching. Their normal world initialization resets all cache certificates before loading payloads.

The central camera view and two cells outside it stay warm, including during reversals. The existing one-cell cosmetic preparation allowance sweeps the approaching offscreen edge instead of rebuilding around the character. All four movement-footprint corners and the swept roll lead remain protected. Object/puzzle data and genuine pits are unchanged.

The common byte-coordinate overlap primitive uses the same strict absolute distances in register-only SM83 code. The exceptional wide-coordinate path remains separate in a ROM bank. A native test checks all ordered byte-coordinate pairs along both axes at ten radii, plus mixed boxes: 1,314,816 comparisons. Late particle drawing retains lifetime updates without calculating unused projections.

Regression observers check actual composed/uploaded pixels, requested-pose progress, visible sprite buffers, complete layer counts, central-view terrain and camera-rim animation. Camera combat timing observes completed updates against real VBlank epochs and rejects late completion; raw line-zero counter samples are retained because ISR-return phase can pair a zero and two without skipping an update.

The stress trace also exposed an EI/HALT boundary race: VBlank could be serviced, then execution returned to HALT and slept until the next speech-timer interrupt. The wait now halts with IME disabled, uses a harmless NOP for the pending-at-HALT duplicate fetch, and enables/services interrupts before checking the epoch again. This retains low-power waiting. The behavior is documented in [Pan Docs](https://github.com/gbdev/pandocs/blob/master/src/halt.md). The regression checks input-start phase as well as completed-update deadlines.

Preparation avoids decoding a multi-tick pose that will advance in the same update, while terminal and one-tick frames remain eligible. Completed poses that did not fit the late cache allowance get the next early allowance before their pixels can be replaced. The fixed-WRAM pixel buffer is aligned without adding RAM, and eight CGB GDMA blocks upload its exact 128 bytes in VBlank; both mapper links gate its alignment. The pixel observer checks the actual completed VRAM upload.

The HUD copies fixed rows in a bounded VBlank handler. Periodic clock arithmetic runs after complete actors when enough time remains; an observer verifies exact elapsed-VBlank arithmetic and bounded polling lag. Late particles advance through a register-only path, checked against their original lifetime, position and velocity rules. Mob overlays use the same allocator and coordinates through a register-only path. One-to-four-column sprite groups clip through the same visible-column rules without the C spill overhead; larger unusual groups keep the portable path. Native observers check all allocation writes against the portable rules and every marker/overlay sprite request against the art rules.

Harness accesses to game data in D000-DFFF explicitly select WRAM bank 1: an emulator display boundary can pause inside a stack-free pose-cache copy with another bank selected. This changes inspection/fixture addressing, without writing the CPU's bank register or advancing extra frames to normalize observations.

Validation and deployment are pending. No physical cartridge has been written.
