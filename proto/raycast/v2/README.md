# Fullscreen raycaster prototype

Run `make test-fs-proto` from the repository root to build the 160x64 view stretched to 160x128, exercise movement
for 1,800 emulated frames, compare 64 views with the host renderer and check the moving-average 20 fps target,
ten-sprite line budget and VBlank boundary. The report is `build/fs-proto-check.json`.

The fullscreen banded-floor configuration enables the floor cache by default. `FLAT=0` selects textured floors
without that default cache. The benchmark separates newly redrawn views (`fps_redrawn`) from retained views;
counting retained views as rendered frames gives a misleading performance figure.

The 2026-10-15 check passes at 20.03 fps for redrawn views with exact host parity. The slowest individual view
takes five display frames, so this is not a minimum-20-fps guarantee. This remains a standalone prototype;
the playable game uses its office room renderer, and the title uses the frozen core under `tools/title3d/core`.
