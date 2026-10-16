# Optional cartridge backup — 2026-10-15

The existing EGRESS writer required a fresh backup receipt before displaying its write acknowledgment. It now offers backup as an optional action and shows a separate explicit acknowledgment when no new backup is retained. The acknowledgment explains replacement of the current game/save and possible permanent loss. Consent resets when selecting another ROM, changing session/cartridge, beginning backup/write, and reloading. No physical device was written during this change.

The no-backup path performs no backup reads or library writes. If the selected ROM overlaps the protected tail, that exact range is read fresh and compared before the first write; differing bytes abort. Cartridge identity, ROM/mapper/capacity checks, exclusive ownership, CRCs, full ROM/save SHA-256 readbacks and no automatic retries remain. The operation record explicitly stores backupSkipped and null backup identities, without claiming a recovery copy exists.

Validation: 29 shared writer/device tests pass, including successful no-backup writes and matching/mismatching protected-tail reads at nonzero offsets. The isolated EGRESS production build passes. Both focused browser tests pass with simulated USB hardware: acknowledged direct write and optional verified-backup write. The browser test also checks acknowledgment reset and journal truthfulness. Screenshot is a browser fixture, not physical hardware evidence.

Published only EGRESS /chromatic and its required static assets, preserving home pages, ROMs and CRUCIBLE. CRUCIBLE currently has no cartridge writer; the canonical shared writer is updated for future consumers. All 30 public asset hashes match. Live page SHA-256: 307ddaf3aab470b3e553838ba5e8e68a3881614af8d075fb991d5a273e12572f. Rollback: /opt/velatis-gateway/backups/chromatic-optional-backup-20261015.

Commits: shared writer `8f542ea`; EGRESS site `18ffe48`.
