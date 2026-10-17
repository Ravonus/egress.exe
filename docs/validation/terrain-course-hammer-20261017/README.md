# Consistent terrain drops, fixed tutorial and physical hazards

Terrain cards now drop from above, settle, and fall downward at the trailing edge. Camera construction never changes the material board. Seven existing OBJ slots cache the canonical tile quadrants, including connected carpet patterns; a separate eighth slot remains available to fragile-floor breaks. The cache uses existing card metadata and adds no hot WRAM. Packed ROM cards permit a single bounded 64-byte upload on a cache miss. Discovery starts half a tile ahead of the screen. Frame admission, one reserved enemy-spawn turn in four, bounded retries and the independent swept-foot safety path keep cosmetic work behind essential play.

The old random rise/slide/glitch arrivals and generic flying-slab substitutions are removed. Cosmetic departures begin their downward flight immediately instead of waiting for a second crack-state BG transaction. Actual fragile surfaces retain their own cracking/shattering effects. Idle palette restoration no longer alternates between the terrain and combat palettes every other frame.

The lobby's TUTORIAL entry opens a fixed playable course, rather than changing a setting. The course practices movement, A/B taps and charges, evasion, pushing a box onto a pressure plate, combining FIRE and WATER into STEAM, crossing a gap and leaving through the keyed exit. START leaves training. Course play bypasses family/run/Continue journal writes; completion stores only a settings marker. Ordinary clock persistence remains active.

New procedural runs include visible interior abyss holes and brittle shoulders beside permanent walking routes. Both physical and recipe strategies remain available. Constructor bits reuse the existing saved slice byte, preserving the exact geography of old Continue journals. Open abyss is lethal; enclosed one-cell pits remain recoverable and serve the training course.

Hammer heirs load distinct wind-up/contact head-and-handle artwork into the existing weapon slots. Cardinal and diagonal rendering follow the same eight-way aim as the hit box. Forward impacts receive a heavy sound, shake and dust; the rear target is untouched in the eight-direction native fixture.

Focused native checks exercise ordinary opening fights, actual downward OBJ trajectories, canonical flight/BG bytes, central visibility and VBlank deadlines; the dedicated course is completed through controller input starting from an existing heir save; hammer fixtures check real art upload and front/rear targets. The camera-body observer now verifies complete emitted poses and follows recent complete shadow lists through the hardware OAM publication delay, rather than inferring the previous displayed pose solely from a newer composition mask.

Release validation and publication results are recorded separately. This work does not declare the full campaign or every sprint complete. No physical cartridge write is part of this release.
