# Native island-edge release evidence

Game source: `78395188c33a4b1298ee9503476b81910fdd3256`. Both final cartridge images are 1,196,032 bytes
(73 banks), leaving 703,968 bytes under the existing ROM-size gate. Exact identities are in `release.json`.

`make -j2 -s test` completed with exit 0. `make-test.log` retains its complete output, including 768 accepted room
replays / 10,724 checks, 192 physical routes, curriculum simulations, all native art states, both clock/save paths,
mapper-write audits and the title's 30 fps loop checks. `gravity.json` records 1,536 native/host gravity matches,
including CRT loss to the abyss and ordinary fillable holes.

The island tests use actual first-run input and generated terrain: walk off the edge, pay one half-heart, return to
firm floor, push a required crate into the abyss, see the reset cue and reset to the original board. The attack and
projectile checks use explicitly labelled target/ground fixtures and ordinary A/B input. All four coordinate
limits are exercised. The stamina reports cover all 545 native bar thresholds and palette restoration after
controls; the bench report checks item consumption, the combination scene and the restored bar tiles/palettes.

Both boss fights retain the original gates: zero damage taken, every phase reached, target hit counts, no scan
line above ten sprites and 120 game frames per 120 display frames throughout active combat. The native test player
now uses a tap-B shot for Fax openings across water, finishes the Fax's guard startup without an accidental
directional evade and times its visible ram for a perfect parry. No game damage, timing window or gate was relaxed.

This remains the ten-floor office development build. The sitting/art and narration changes from the preceding
feedback milestone are retained. Larger connected stages, composed portal dependencies, the full run and the
other unfinished items remain on `docs/SPRINTS.md`. No physical Chromatic test was performed for this release.
