# Paired reconnect and acknowledgment — 2026-10-15

Both sites now automatically attempt one verified reconnection for a single previously permitted console/bridge when the page opens or the hardware connects. No permission chooser opens automatically. Existing manual and automatic actions reuse the same opening and response verification functions. Multiple matching devices require selection; manual Disconnect suppresses reconnect for that page; interrupted operations require manual review/reconnection and never resume. Errors retain the endpoint-specific cause and explain another tab/tool may own the device. No firmware/cartridge operation is triggered automatically.

The EGRESS acknowledgment checkbox is now independently clickable whenever no operation is running. Connection and ROM compatibility conditions still gate Flash. The page explains missing connection/identity and resets consent on device changes. The earlier mandatory backup remains optional.

Both production builds pass. EGRESS: 8 focused browser tests pass (five auto-connect cases, two complete simulated write paths, one label-click/keyboard acknowledgment before connecting). CRUCIBLE: 5 auto-connect cases pass. Tests cover startup/reload, physical-connect events, occupied handles, multiple matching devices, manual disconnect, no permission prompts and no automatic cartridge commands. Screenshots are browser-fixture evidence.

Published only /chromatic and required static assets on both sites, with guarded previous hashes and rollback copies at /opt/velatis-gateway/backups/chromatic-reconnect-20261015. All 63 public asset hashes match. Commits: canonical 6c9d59b; EGRESS 54433e4; CRUCIBLE 02b1982.

Physical observation: an older Chrome tab held both device connections while a newer tab reported claimInterface failure. Later the older tab was closed by the user and the remaining tab showed an active user-started cartridge write at 79 percent. No browser reload, disconnect, reset, flash or second device claim was performed by the agent. Real auto-reconnection remains untested because interrupting that active write would be inappropriate; the existing write was left running unchanged.
