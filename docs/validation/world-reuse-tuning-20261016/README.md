# Bounded tile reuse: final timing pass

This continues `world-stream-20261016` with the same fixed active window, cached four-cell strips and eight-slot
animation pool. No general mesh manager or extra world buffer was added. The public release is recorded
separately; this evidence covers the exact native images and source hashes in `manifest.json`.

Retained vertical card spans now move through one bounded near leaf instead of repeated indexed word
operations. A read-only spatial oracle checks every retained/recycled card, both signed spans, compacted queue
and all five presentation bitmaps through actual player-input transfers. Native terrain hashing uses equivalent
byte operations; the independent host constructor retains its original word arithmetic. The earlier frame
preparation deadline moves from scan line 25 to 35 after recovering that transfer headroom.

Both mapper images pass all 42 generated-world input scenarios, both actual lost-box/reset cases and the complete
`make -j1 test` regression. Each world route retains strict 120/120 game/display frames, complete bodies,
independently composed heir pixels, native BG/attribute agreement, OAM limits and the stack canary. The maximum
observed real window-margin wait falls from 34 to 8 frames. The input gate now rejects waits over 12 frames.

Terrain preparation can still briefly delay movement at some large-map corners. These are quiet puzzle worlds;
combat streaming, campaign-wide goals/biomes, the complete run, roster, stats, music and first-person stages
remain in `docs/SPRINTS.md`. Emulator evidence does not establish physical Chromatic behavior.

Frozen ROMs, maps, symbols, reports and the full log are retained in
`build/candidates/world-reuse-tuning-20261016/`.

An independent local `make -j4 all` reproduced both offloaded, fully tested ROMs byte for byte.

The validated milestone was merged through PR #1 and deployed from `70a9ecb`; see `site-release.json` for public
file read-back, live browser/download checks and real older-save carryover. All remaining local game branch
heads are included in main; the primary checkout and unrelated native-UI notes are preserved.
