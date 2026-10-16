# Streamed full-run constructor

`make test-run-stream test-native-run-stream` checks the canonical constructor in a compact form suitable for the
cartridge. A 40-byte world header and one 12-byte scratch slot replace a 904-byte complete layout in live memory.
The synchronous ROM0 sink stores each completed slot in a banked WRAM sector; each transfer restores bank 1.

The host compares 2,048 streamed layouts to the complete constructor and to the fixed digest measured from the
constructor before refactoring. The standalone CGB ROM builds and reads back 24 layouts byte for byte against the
host, including a fresh 52-floor story, shortcuts, cycles and descent. Its canary uses DDD0, the game's actual
560-byte stack boundary. The harness advances checkpoints through ordinary joypad input; it does not patch CPU
state or invoke functions. Reports include the exact test ROM hash.

This is a full-run prerequisite. The playable cartridge still runs the office slice; biome content, seed banks,
encounter integration and persistent progression remain on the completion ledger.
