# Continuing animation and peripheral construction

The previous release could defer a new full heir pose every time the draw pass ran late. A real holdout/movement reproduction left a requested pose unserved for 38 active samples while its logical animation advanced. Keeping all body layers visible did not prove that their animation progressed.

Deferred poses now get an early opportunity before speculative terrain work. Gravity and action replacement have priority, and intentionally invisible damage-blink/fall frames do not spend time composing hidden bodies. A pending complete upload cannot be overwritten by a second composition. The compressed stream stops after its final selected gene, including selected empty deltas.

Office combat reuses inactive world-memory banks 2 through 5 for 120 complete composed poses in 60 two-way sets. Cache leaves run without stack access while SVBK is switched, restore SVBK before returning, and run within a critical section. The pixel buffer and cache enable flag stay in fixed WRAM. A new expression disables reuse; floor sprite initialization clears the keys. Generated worlds keep exclusive ownership of those banks and bypass pose caching. Their normal world initialization resets all cache certificates before loading payloads.

The central camera view and two cells outside it stay warm, including during reversals. The existing one-cell cosmetic preparation allowance sweeps the approaching offscreen edge instead of rebuilding around the character. All four movement-footprint corners and the swept roll lead remain protected. Object/puzzle data and genuine pits are unchanged.

The common byte-coordinate overlap primitive uses the same strict absolute distances in register-only SM83 code. The exceptional wide-coordinate path remains separate in a ROM bank. A native test checks all ordered byte-coordinate pairs along both axes at ten radii, plus mixed boxes: 1,314,816 comparisons. Late particle drawing retains lifetime updates without calculating unused projections.

Regression observers check actual composed/uploaded pixels, requested-pose progress, visible sprite buffers, complete layer counts, central-view terrain and camera-rim animation. Camera combat timing observes completed updates against real VBlank epochs and rejects late completion; raw line-zero counter samples are retained because ISR-return phase can pair a zero and two without skipping an update.

Validation and deployment are pending. No physical cartridge has been written.
