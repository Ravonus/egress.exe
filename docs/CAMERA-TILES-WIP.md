# Camera terrain development checkpoint (2026-10-16)

This branch is unfinished. Do not publish it or describe it as a larger connected stage. Main's tested release
is separate. The actual character delta-stream correction is independently committed as `82b7f0d`.

The camera presentation owns a hidden-cell bitset and a bounded assembly/collapse queue. It preserves material
cells, finite pickups and moved crates while travelling and returning. Boss arenas, dark stages and chase stages
retain their own presentation/destruction rules. Forward diagonal prefetch is included but still needs a dedicated
travel/reversal check. See `CONNECTED-STAGES.md` for the separate larger-world implementation requirements.

The native quiet-room route collects a real ingredient, pushes a crate, travels both ways, and reverses a real
forward evade: 1,051 displayed frames, 120 game frames per 120, no missing body/floor, ten or fewer sprites per
line, intact stack canary. Both mappers passed before the final art stream correction; rerun on the next candidate.

Crowded combat is NOT accepted. The stronger `camera_combat.py` waits for actual holdout waves (three live enemies),
travels and uses both actions. The latest combined candidate `edd335a7e609126abb9b229b5933b2baf59c3ce2fe9971269637dd3b8688d016`
reached only 74 game frames in its worst 120-frame window, with ten sprites maximum. Its simple attack bot did not
kill an enemy in that run; conditional enemy defences and frame-dependent movement make the route vary. The prior
public island ROM also fails this stronger timing test (76/120 and a 13-sprite line). Do not weaken the acceptance
gate to call the camera complete. A previous camera candidate also regressed THE TENANT's no-hit/timing proof.

Current changes include atomic publication of complete shadow OAM, an observed register-only allocator path,
shared band reservations, separately banked mob rendering, faster repeated-cell mob collision probes, an epoch
guard for late cosmetic work, camera-specific frame scheduling, sliced dead-room initialization and a native
weighted-plate scan. The portable host gravity reference matched 1,536 expanded native cases, including occupied
plates. All 768 accepted rooms and the 1,000-run curriculum passed with the sliced dead-room implementation.

The read-only `oam_observer.py` compares actual allocation calls/returns, register results, bands and shadow OAM
against the independent C rules, including clipping and refusal. It never calls functions or patches CPU state.
`boss.py` now observes allocations too; avoid registering another hook at `_oam_group` in the same profiling run.
The heir pixel observer caught and proved the independent empty-delta bug; its main release must not depend on
these camera changes.

Commands after checking `uptime`:

```
make -j2 all
.venv/bin/python test/harness/camera_tiles.py build/egress.gbc build/sprints/camera-mbc5
.venv/bin/python test/harness/camera_tiles.py build/egress-rtc.gbc build/sprints/camera-mbc3
.venv/bin/python test/harness/camera_combat.py build/egress.gbc build/sprints/camera-combat-mbc5
.venv/bin/python test/harness/boss.py build/egress.gbc build/sprints/camera-bosses
make -j2 test
```

Useful ignored development evidence remains in `build/sprints/`, including `profile-camera.py`,
`profile-combat-fine.py`, reports and cycle traces. `/tmp/egress-camera-*.log` contains the investigation logs.
Never substitute these combined WIP ROMs for the separately frozen main release when staging site downloads.
