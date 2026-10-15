Mock-up scripts for docs/redesign.md. They compose screens from PyBoy captures of the real CRUCIBLE ROM
(slot-frame tiles, sky tiles, item icons, font) and the ported avatar generator (build/host/avatar_dump).
1. Capture: run grab.py from ~/dev/crucible.exe/test/harness with OUT=build/cru_caps (75 discoveries, 120 browse
   frames), plus CRUCIBLE's capture_flow.py into build/cru_caps/cruflow and crugrab/ (paths as in font.py SRC).
2. CRU_CAPS=build/cru_caps python3 tools/mock/mock_room.py (etc.); outputs land in the working directory.
